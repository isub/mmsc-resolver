UTL_DIR		=../../utils
UTL_H		= -I../..
MBUNI_H		= -I/home/mmsc/mmsc_src/mbuni-1.6.0
MBUNI_UTL_H	= -I/home/mmsc/mmsc_src/mbuni-1.6.0/mmlib
KANNEL_H	= -I/usr/local/include/kannel
KANNEL_LIB_DIR	= -L/usr/local/lib/kannel
KANNEL_LIB	= -lgwlib
LIBXML_H	= -I/home/mmsc/mmsc_src/libxml2-2.7.6/include

mmsc-resolver: ./obj/mmsc-resolver.o ./obj/resolver-operations.o ./obj/resolver-auxiliary.o ./obj/config.o ./obj/log.o
	gcc -shared -o ../libmmsc-resolver.so $(KANNEL_LIB_DIR) $(KANNEL_LIB) ./obj/mmsc-resolver.o ./obj/resolver-operations.o ./obj/resolver-auxiliary.o ./obj/config.o ./obj/log.o -lcurl -lmms -lstdc++
./obj/mmsc-resolver.o: mmsc-resolver.c ./obj/resolver-auxiliary.o ./obj/resolver-operations.o
	gcc -fPIC -c mmsc-resolver.c -o ./obj/mmsc-resolver.o $(MBUNI_H) $(MBUNI_UTL_H) $(KANNEL_H) $(LIBXML_H)
./obj/resolver-operations.o: resolver-operations.cpp
	g++ -fPIC -c resolver-operations.cpp $(UTL_H) -o ./obj/resolver-operations.o
./obj/resolver-auxiliary.o: resolver-auxiliary.cpp
	g++ -fPIC -c resolver-auxiliary.cpp $(UTL_H) -o ./obj/resolver-auxiliary.o
./obj/config.o: $(UTL_DIR)/config/config.cpp
	g++ -fPIC -c $(UTL_DIR)/config/config.cpp -o ./obj/config.o
./obj/log.o: $(UTL_DIR)/log/log.cpp
	g++ -fPIC -c $(UTL_DIR)/log/log.cpp -o ./obj/log.o

install:
	cp -f ../*.so /usr/local/lib

clean:
	rm -f ./obj/* ../*.so