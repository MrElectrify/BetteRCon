g++ --std=c++17 -fPIC -Wall -I../include -I../dependencies/asio/asio/include -I../dependencies/MD5 ../src/Internal/Connection.cpp ../src/Internal/ErrorCode.cpp ../src/Internal/Packet.cpp ../src/Server.cpp ../dependencies/MD5/MD5.cpp -c
ar rcs libBetteRConFramework.a Connection.o ErrorCode.o Packet.o Server.o MD5.o
mv libBetteRConFramework.a lib/
rm *.o