all:
	g++ -std=c++11 -o client client.cpp -lev
	g++ -std=c++11 -o wwwd server.cpp -lev
	g++ -std=c++11 -o proxy proxy.cpp -lev