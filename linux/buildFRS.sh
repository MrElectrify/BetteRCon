g++ --std=c++17 -fPIC -I../include -I../dependencies/asio/asio/include -Llib ../src/FastRoundStart.cpp -Wl,-Bstatic -lBetteRConFramework -Wl,-Bdynamic -lpthread -ldl -lstdc++fs -shared -oplugins/FastRoundStart.plugin