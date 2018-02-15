DEBUG=True
BUILD_ROOT=./build
CLIENT_BUILD_DIR=$(BUILD_ROOT)/client
SERVER_BUILD_DIR=$(BUILD_ROOT)/server
RELATIVE_ROOT_DIR=../../..
TEST_OS=osx

.PHONY: client
client:
	-mkdir -p $(CLIENT_BUILD_DIR)/$(TEST_OS)
	cd $(CLIENT_BUILD_DIR)/$(TEST_OS) && (rm client bench roomcl echo || true) && cmake -DDEBUG:BOOL=$(DEBUG) -DTEST_OS:STRING=$(TEST_OS) $(RELATIVE_ROOT_DIR)/client && make

.PHONY: server
server:
	-mkdir -p $(SERVER_BUILD_DIR)/$(TEST_OS)
	cd $(SERVER_BUILD_DIR)/$(TEST_OS) && (rm server roomsv || true) && cmake -DDEBUG:BOOL=$(DEBUG) -DTEST_OS:STRING=$(TEST_OS) $(RELATIVE_ROOT_DIR)/server && make

test: server client test_e2e test_chaos test_reconnect test_bench

test_e2e:
	@echo "---- test e2e ----"
	ulimit -c unlimited && ulimit -n 2048 && $(SERVER_BUILD_DIR)/$(TEST_OS)/server &
	ulimit -c unlimited && ulimit -n 2048 && $(CLIENT_BUILD_DIR)/$(TEST_OS)/client
	sleep 1

test_chaos:
	@echo "---- test chaos ----"
	ulimit -c unlimited && ulimit -n 2048 && ($(SERVER_BUILD_DIR)/$(TEST_OS)/server & echo $$! > server.pid) &
	ulimit -c unlimited && ulimit -n 2048 && CHAOS=1 $(CLIENT_BUILD_DIR)/$(TEST_OS)/echo 0 5
	kill `cat server.pid` && rm server.pid
	sleep 1

test_reconnect:
	@echo "---- test reconnect ----"
	ulimit -c unlimited && ulimit -n 2048 && ($(SERVER_BUILD_DIR)/$(TEST_OS)/roomsv 4 & echo $$! > server.pid) 2>/dev/null &
	ulimit -c unlimited && ulimit -n 2048 && $(CLIENT_BUILD_DIR)/$(TEST_OS)/roomcl 0 256 2>/dev/null 
	kill `cat server.pid` && rm server.pid
	sleep 1

test_bench:
	@echo "---- test bench ----"
	ulimit -c unlimited && ulimit -n 2048 && ($(SERVER_BUILD_DIR)/$(TEST_OS)/server 4 & echo $$! > server.pid) 2>/dev/null &
	ulimit -c unlimited && ulimit -n 2048 && $(CLIENT_BUILD_DIR)/$(TEST_OS)/bench 2>/dev/null
	kill `cat server.pid` && rm server.pid
	sleep 1

clean:
	rm -rf $(CLIENT_BUILD_DIR)/$(TEST_OS)
	rm -rf $(SERVER_BUILD_DIR)/$(TEST_OS)

clean_all:
	rm -rf $(BUILD_ROOT)
