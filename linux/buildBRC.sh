[ ! -d "bin/" ] && mkdir bin
g++ --std=c++17 -I../include -I../dependencies/asio/asio/include -Llib ../src/BetteRConConsole.cpp -Wl,-Bstatic -lBetteRConFramework -Wl,-Bdynamic -lpthread -ldl -lstdc++fs -obin/BetteRConConsole -fsanitize=undefined,address
