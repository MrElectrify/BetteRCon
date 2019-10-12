g++ -c --std=c++17 src/*.cpp src/Internal/*.cpp -Iinclude -I../../asio/asio/include
ar rvs libBetteRConFramework.lib *.o
