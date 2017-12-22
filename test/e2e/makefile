DEBUG=True
BUILD_ROOT=./build
CLIENT_BUILD_DIR=$(BUILD_ROOT)/client
SERVER_BUILD_DIR=$(BUILD_ROOT)/server
RELATIVE_ROOT_DIR=../../..
TEST_OS=osx

.PHONY: client
client:
	-mkdir -p $(CLIENT_BUILD_DIR)/$(TEST_OS)
	cd $(CLIENT_BUILD_DIR)/$(TEST_OS) && (rm client bench || true) && cmake -DDEBUG:BOOL=$(DEBUG) -DTEST_OS:STRING=$(TEST_OS) $(RELATIVE_ROOT_DIR)/client && make

.PHONY: server
server:
	-mkdir -p $(SERVER_BUILD_DIR)/$(TEST_OS)
	cd $(SERVER_BUILD_DIR)/$(TEST_OS) && (rm server || true) && cmake -DDEBUG:BOOL=$(DEBUG) -DTEST_OS:STRING=$(TEST_OS) $(RELATIVE_ROOT_DIR)/server && make

test: server client
	ulimit -n 2048 && ($(SERVER_BUILD_DIR)/$(TEST_OS)/server & echo $$! > server.pid) &
	ulimit -n 2048 && $(CLIENT_BUILD_DIR)/$(TEST_OS)/client
	kill `cat server.pid` && rm server.pid

clean:
	rm -rf $(CLIENT_BUILD_DIR)/$(TEST_OS)
	rm -rf $(SERVER_BUILD_DIR)/$(TEST_OS)

clean_all:
	rm -rf $(BUILD_ROOT)
