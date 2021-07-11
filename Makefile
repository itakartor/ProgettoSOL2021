CC = gcc
CFLAGS = -g
CONFIGFILE1 = ./configs/config1.txt
CLIENTOUTPUT_DIR1 = ./output/outputTest1
CLIENTOUTPUT_DIR2 = ./output/outputTest2
FLAG_INCLUDE_DIR = -I ./includes/
SOCKNAME = ./mysock.sk
TARGETS = server client 
VALGRIND_OPTIONS = valgrind --leak-check=full

SERVER_DEPS = server.c util.c queue.c


CLIENT_DEPS = client.c util.c queue.c Parser.c


CLIENTCONFIGOPTIONS = -f $(SOCKNAME) -p -t 200



#configurazioni di un client per testare singolarmente
# le funzioni dell'API
CONFTEST1OPENFILE = -i .,1 -i .,1 -o fileDaLeggere.txt,clientConfig.c
CONFTEST1CLOSEFILE = -i .,2 -C fileDaLeggere.txt,clientConfig.c,clientConfig.c
CONFTEST1READFILE = -d ./TestFileBinari -i .,2 -C clientConfig.c \
 -r fileDaLeggere.txt,clientConfig.c,clientConfig.h
CONFTEST1READNFILESWRONG = -d ./Pippo -i .,5 -R0
CONFTEST1READNFILESRIGHT = -d ./TestFileBinari -i .,5 -R0
CONFTEST1REMOVEFILE = -i .,5 -c fileDaLeggere.txt,clientConfig.c
CONFTEST1WRITEFILE = -i .,5 \
	-W clientConfig.h,pippo.txt,serverTestScript.sh \
	-C descriptor.h -W descriptor.h -o descriptor.h \
	-W descriptor.h

.PHONY : all, cleanall, test1, test2

all:
	make -B server
	make -B client

cleanall:
	rm -rf $(SOCKNAME) $(TARGETS)
	rm $(CLIENTOUTPUT_DIR1)/*
#rmdir $(CLIENTOUTPUT_DIR1)
	rm $(CLIENTOUTPUT_DIR2)/*
#rmdir $(CLIENTOUTPUT_DIR2)

#targets per generare gli eseguibili
server: $(SERVER_DEPS)
		#make cleanall
		$(CC) $(CFLAGS) $(SERVER_DEPS) -pthread -o server $(FLAG_INCLUDE_DIR)
				   

client: $(CLIENT_DEPS)
		#make cleanall
		$(CC) $(CFLAGS) $(CLIENT_DEPS) -pthread -o client $(FLAG_INCLUDE_DIR)



test1:
	chmod +x ./Test/TestScript1.sh
	./Test/TestScript1.sh $(CLIENTOUTPUT_DIR1)

test2:
	./Scripts/serverTest2.sh $(CLIENTOUTPUT_DIR2)