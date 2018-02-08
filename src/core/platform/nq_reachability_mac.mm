#include "core/platform/nq_reachability.h"
#include "basis/assert.h"

#include <netinet/in.h>

#import <Foundation/Foundation.h>
#import <SystemConfiguration/SystemConfiguration.h>


//objc object definition
typedef NS_ENUM(NSInteger, NetworkStatus) {
    // Apple NetworkStatus Compatible Names.
    NotReachable = 0,
    ReachableViaWiFi = 2,
    ReachableViaWWAN = 1
};

@class Reachability;

typedef void (^NetworkReachable)(Reachability * reachability);
typedef void (^NetworkUnreachable)(Reachability * reachability);
typedef void (^NetworkReachability)(Reachability * reachability, SCNetworkConnectionFlags flags);

@interface Reachability : NSObject

@property (nonatomic, copy) NetworkReachable    reachableBlock;
@property (nonatomic, copy) NetworkUnreachable  unreachableBlock;
@property (nonatomic, copy) NetworkReachability reachabilityBlock;

@property (nonatomic, assign) BOOL reachableOnWWAN;


+(instancetype)reachabilityWithHostname:(const char*)hostname;
+(instancetype)reachabilityForInternetConnection;
+(instancetype)reachabilityWithAddress:(void *)hostAddress;
+(instancetype)reachabilityForLocalWiFi;

-(instancetype)initWithReachabilityRef:(SCNetworkReachabilityRef)ref;

-(BOOL)startNotifier:(void *)context_ptr;
-(void)stopNotifier;

-(BOOL)isReachable;
-(BOOL)isReachableViaWWAN;
-(BOOL)isReachableViaWiFi;

// WWAN may be available, but not active until a connection has been established.
// WiFi may require a connection for VPN on Demand.
-(BOOL)isConnectionRequired; // Identical DDG variant.
-(BOOL)connectionRequired; // Apple's routine.
// Dynamic, on demand connection?
-(BOOL)isConnectionOnDemand;
// Is user intervention required?
-(BOOL)isInterventionRequired;

-(NetworkStatus)currentReachabilityStatus;
-(SCNetworkReachabilityFlags)reachabilityFlags;
-(NSString*)currentReachabilityString;
-(NSString*)currentReachabilityFlags;

@end

//objc implementation
@interface Reachability ()

@property (nonatomic, assign) SCNetworkReachabilityRef  reachabilityRef;
@property (nonatomic) dispatch_queue_t          reachabilitySerialQueue;
@property (nonatomic, strong) id                        reachabilityObject;

-(void)reachabilityChanged:(SCNetworkReachabilityFlags)flags;
-(BOOL)isReachableWithFlags:(SCNetworkReachabilityFlags)flags;

@end


static NSString *reachabilityFlags(SCNetworkReachabilityFlags flags) 
{
    return [NSString stringWithFormat:@"%c%c %c%c%c%c%c%c%c",
#if TARGET_OS_IPHONE
            (flags & kSCNetworkReachabilityFlagsIsWWAN)               ? 'W' : '-',
#else
            'X',
#endif
            (flags & kSCNetworkReachabilityFlagsReachable)            ? 'R' : '-',
            (flags & kSCNetworkReachabilityFlagsConnectionRequired)   ? 'c' : '-',
            (flags & kSCNetworkReachabilityFlagsTransientConnection)  ? 't' : '-',
            (flags & kSCNetworkReachabilityFlagsInterventionRequired) ? 'i' : '-',
            (flags & kSCNetworkReachabilityFlagsConnectionOnTraffic)  ? 'C' : '-',
            (flags & kSCNetworkReachabilityFlagsConnectionOnDemand)   ? 'D' : '-',
            (flags & kSCNetworkReachabilityFlagsIsLocalAddress)       ? 'l' : '-',
            (flags & kSCNetworkReachabilityFlagsIsDirect)             ? 'd' : '-'];
}

// Start listening for reachability notifications on the current run loop
static void TMReachabilityCallback(SCNetworkReachabilityRef target, SCNetworkReachabilityFlags flags, void* info);

@implementation Reachability

#pragma mark - Class Constructor Methods

+(instancetype)reachabilityWithHostname:(const char *)hostname
{
    SCNetworkReachabilityRef ref = SCNetworkReachabilityCreateWithName(NULL, hostname);
    if (ref) 
    {
        id reachability = [[self alloc] initWithReachabilityRef:ref];

        return reachability;
    }
    
    return nil;
}

+(instancetype)reachabilityWithAddress:(void *)hostAddress
{
    SCNetworkReachabilityRef ref = SCNetworkReachabilityCreateWithAddress(kCFAllocatorDefault, (const struct sockaddr*)hostAddress);
    if (ref) 
    {
        id reachability = [[self alloc] initWithReachabilityRef:ref];
        
        return reachability;
    }
    
    return nil;
}

+(instancetype)reachabilityForInternetConnection
{
    struct sockaddr_in zeroAddress;
    bzero(&zeroAddress, sizeof(zeroAddress));
    zeroAddress.sin_len = sizeof(zeroAddress);
    zeroAddress.sin_family = AF_INET;
    
    return [self reachabilityWithAddress:&zeroAddress];
}

+(instancetype)reachabilityForLocalWiFi
{
    struct sockaddr_in localWifiAddress;
    bzero(&localWifiAddress, sizeof(localWifiAddress));
    localWifiAddress.sin_len            = sizeof(localWifiAddress);
    localWifiAddress.sin_family         = AF_INET;
    // IN_LINKLOCALNETNUM is defined in <netinet/in.h> as 169.254.0.0
    localWifiAddress.sin_addr.s_addr    = htonl(IN_LINKLOCALNETNUM);
    
    return [self reachabilityWithAddress:&localWifiAddress];
}


// Initialization methods

-(instancetype)initWithReachabilityRef:(SCNetworkReachabilityRef)ref
{
    self = [super init];
    if (self != nil) 
    {
        self.reachableOnWWAN = YES;
        self.reachabilityRef = ref;

        // We need to create a serial queue.
        // We allocate this once for the lifetime of the notifier.

        self.reachabilitySerialQueue = dispatch_queue_create("com.tonymillion.reachability", NULL);
    }
    
    return self;    
}

-(void)dealloc
{
    [self stopNotifier];

    if(self.reachabilityRef)
    {
        CFRelease(self.reachabilityRef);
        self.reachabilityRef = nil;
    }

    self.reachableBlock          = nil;
    self.unreachableBlock        = nil;
    self.reachabilityBlock       = nil;
    self.reachabilitySerialQueue = nil;

    [super dealloc];
}

#pragma mark - Notifier Methods

// Notifier 
// NOTE: This uses GCD to trigger the blocks - they *WILL NOT* be called on THE MAIN THREAD
// - In other words DO NOT DO ANY UI UPDATES IN THE BLOCKS.
//   INSTEAD USE dispatch_async(dispatch_get_main_queue(), ^{UISTUFF}) (or dispatch_sync if you want)

-(BOOL)startNotifier:(void *)context_ptr
{
    // allow start notifier to be called multiple times
    if(self.reachabilityObject && (self.reachabilityObject == self))
    {
        return YES;
    }


    SCNetworkReachabilityContext    context = { 0, NULL, NULL, NULL, NULL };
    context.info = context_ptr;

    if(SCNetworkReachabilitySetCallback(self.reachabilityRef, TMReachabilityCallback, &context))
    {
        // Set it as our reachability queue, which will retain the queue
        if(SCNetworkReachabilitySetDispatchQueue(self.reachabilityRef, self.reachabilitySerialQueue))
        {
            // this should do a retain on ourself, so as long as we're in notifier mode we shouldn't disappear out from under ourselves
            // woah
            self.reachabilityObject = self;
            return YES;
        }
        else
        {
#ifdef DEBUG
            NSLog(@"SCNetworkReachabilitySetDispatchQueue() failed: %s", SCErrorString(SCError()));
#endif

            // UH OH - FAILURE - stop any callbacks!
            SCNetworkReachabilitySetCallback(self.reachabilityRef, NULL, NULL);
        }
    }
    else
    {
#ifdef DEBUG
        NSLog(@"SCNetworkReachabilitySetCallback() failed: %s", SCErrorString(SCError()));
#endif
    }

    // if we get here we fail at the internet
    self.reachabilityObject = nil;
    return NO;
}

-(void)stopNotifier
{
    // First stop, any callbacks!
    SCNetworkReachabilitySetCallback(self.reachabilityRef, NULL, NULL);
    
    // Unregister target from the GCD serial dispatch queue.
    SCNetworkReachabilitySetDispatchQueue(self.reachabilityRef, NULL);

    self.reachabilityObject = nil;
}

#pragma mark - reachability tests

// This is for the case where you flick the airplane mode;
// you end up getting something like this:
//Reachability: WR ct-----
//Reachability: -- -------
//Reachability: WR ct-----
//Reachability: -- -------
// We treat this as 4 UNREACHABLE triggers - really apple should do better than this

#define testcase (kSCNetworkReachabilityFlagsConnectionRequired | kSCNetworkReachabilityFlagsTransientConnection)

-(BOOL)isReachableWithFlags:(SCNetworkReachabilityFlags)flags
{
    BOOL connectionUP = YES;
    
    if(!(flags & kSCNetworkReachabilityFlagsReachable))
        connectionUP = NO;
    
    if( (flags & testcase) == testcase )
        connectionUP = NO;
    
#if TARGET_OS_IPHONE
    if(flags & kSCNetworkReachabilityFlagsIsWWAN)
    {
        // We're on 3G.
        if(!self.reachableOnWWAN)
        {
            // We don't want to connect when on 3G.
            connectionUP = NO;
        }
    }
#endif
    
    return connectionUP;
}

-(BOOL)isReachable
{
    SCNetworkReachabilityFlags flags;  
    
    if(!SCNetworkReachabilityGetFlags(self.reachabilityRef, &flags))
        return NO;
    
    return [self isReachableWithFlags:flags];
}

-(BOOL)isReachableViaWWAN 
{
#if TARGET_OS_IPHONE

    SCNetworkReachabilityFlags flags = 0;
    
    if(SCNetworkReachabilityGetFlags(self.reachabilityRef, &flags))
    {
        // Check we're REACHABLE
        if(flags & kSCNetworkReachabilityFlagsReachable)
        {
            // Now, check we're on WWAN
            if(flags & kSCNetworkReachabilityFlagsIsWWAN)
            {
                return YES;
            }
        }
    }
#endif
    
    return NO;
}

-(BOOL)isReachableViaWiFi 
{
    SCNetworkReachabilityFlags flags = 0;
    
    if(SCNetworkReachabilityGetFlags(self.reachabilityRef, &flags))
    {
        // Check we're reachable
        if((flags & kSCNetworkReachabilityFlagsReachable))
        {
#if TARGET_OS_IPHONE
            // Check we're NOT on WWAN
            if((flags & kSCNetworkReachabilityFlagsIsWWAN))
            {
                return NO;
            }
#endif
            return YES;
        }
    }
    
    return NO;
}


// WWAN may be available, but not active until a connection has been established.
// WiFi may require a connection for VPN on Demand.
-(BOOL)isConnectionRequired
{
    return [self connectionRequired];
}

-(BOOL)connectionRequired
{
    SCNetworkReachabilityFlags flags;
  
  if(SCNetworkReachabilityGetFlags(self.reachabilityRef, &flags))
    {
    return (flags & kSCNetworkReachabilityFlagsConnectionRequired);
  }
    
    return NO;
}

// Dynamic, on demand connection?
-(BOOL)isConnectionOnDemand
{
  SCNetworkReachabilityFlags flags;
  
  if (SCNetworkReachabilityGetFlags(self.reachabilityRef, &flags))
    {
    return ((flags & kSCNetworkReachabilityFlagsConnectionRequired) &&
        (flags & (kSCNetworkReachabilityFlagsConnectionOnTraffic | kSCNetworkReachabilityFlagsConnectionOnDemand)));
  }
  
  return NO;
}

// Is user intervention required?
-(BOOL)isInterventionRequired
{
    SCNetworkReachabilityFlags flags;
  
  if (SCNetworkReachabilityGetFlags(self.reachabilityRef, &flags))
    {
    return ((flags & kSCNetworkReachabilityFlagsConnectionRequired) &&
        (flags & kSCNetworkReachabilityFlagsInterventionRequired));
  }
  
  return NO;
}


#pragma mark - reachability status stuff

-(NetworkStatus)currentReachabilityStatus
{
    if([self isReachable])
    {
        if([self isReachableViaWiFi])
            return ReachableViaWiFi;
        
#if TARGET_OS_IPHONE
        return ReachableViaWWAN;
#endif
    }
    
    return NotReachable;
}

-(SCNetworkReachabilityFlags)reachabilityFlags
{
    SCNetworkReachabilityFlags flags = 0;
    
    if(SCNetworkReachabilityGetFlags(self.reachabilityRef, &flags)) 
    {
        return flags;
    }
    
    return 0;
}

-(NSString*)currentReachabilityString
{
  NetworkStatus temp = [self currentReachabilityStatus];
  
  if(temp == ReachableViaWWAN)
  {
        // Updated for the fact that we have CDMA phones now!
    return NSLocalizedString(@"Cellular", @"");
  }
  if (temp == ReachableViaWiFi) 
  {
    return NSLocalizedString(@"WiFi", @"");
  }
  
  return NSLocalizedString(@"No Connection", @"");
}

-(NSString*)currentReachabilityFlags
{
    return reachabilityFlags([self reachabilityFlags]);
}

#pragma mark - Callback function calls this method

-(void)reachabilityChanged:(SCNetworkReachabilityFlags)flags
{
    if([self isReachableWithFlags:flags])
    {
        if(self.reachableBlock)
        {
            self.reachableBlock(self);
        }
    }
    else
    {
        if(self.unreachableBlock)
        {
            self.unreachableBlock(self);
        }
    }
    
    if(self.reachabilityBlock)
    {
        self.reachabilityBlock(self, flags);
    }    
}
@end

namespace net {

class NqReachabilityMac : public NqReachability {
 public:
  bool Start(const std::string &hostname) override {
    if (hostname.length() <= 0) {
      reachability_ = [Reachability reachabilityForInternetConnection];
    } else {
      reachability_ = [Reachability reachabilityWithHostname:hostname.c_str()];
    }
    if (!reachability_) {
      return false;
    }

    return [reachability_ startNotifier:this];
  }
  void Stop() override {
    if (!reachability_) {
      return;
    }
    [reachability_ stopNotifier];
  }
  void OnChange(SCNetworkReachabilityFlags flags) {
    current_state_ = ToNqReachability([reachability_ currentReachabilityStatus]);
    nq_closure_call(observer_, on_reachability_change, current_state_);
  }
  NqReachabilityMac(nq_closure_t cb) : NqReachability(cb), reachability_(nullptr) {}
 protected:
  ~NqReachabilityMac() override {
    if (reachability_) {
      [reachability_ dealloc];
    }
  }
  nq_reachability_t ToNqReachability(NetworkStatus status) {
    switch (status) {
      case NotReachable:
        return NQ_NOT_REACHABLE;
      case ReachableViaWWAN:
        return NQ_REACHABLE_WWAN;
      case ReachableViaWiFi:
        return NQ_REACHABLE_WIFI;
      default:
        ASSERT(false);
        return NQ_NOT_REACHABLE;
    }
  }

  Reachability *reachability_;
};

NqReachability *NqReachability::Create(nq_closure_t cb) {
  return new NqReachabilityMac(cb);
}
}
void TMReachabilityCallback(SCNetworkReachabilityRef target, SCNetworkReachabilityFlags flags, void* info) 
{
#pragma unused (target)

    net::NqReachabilityMac *reachability = ((net::NqReachabilityMac*)info);
    reachability->OnChange(flags);
}
