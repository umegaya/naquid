DEBUG=True

.PHONY: client
client:
	-mkdir -p ./client/build
	cd client/build && (rm client || true) && cmake -DDEBUG:BOOL=$(DEBUG) .. && make

.PHONY: server
server:
	-mkdir -p ./server/build
	cd server/build && (rm server || true) && cmake -DDEBUG:BOOL=$(DEBUG) .. && make

test: server client
	ulimit -n 2048 && cd ./server/build && ./server &
	ulimit -n 2048 && cd ./client/build && ./client
	killall server

clean:
	rm -rf ./client/build
	rm -rf ./server/build
