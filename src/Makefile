CC = gcc
CFLAGS = -g -pthread
CONFIGFILE1 = ./configs/config1.txt
CONFIGFILE2 = ./configs/config2.txt
CLIENTOUTPUT_DIR1 = ./output/outputTest1
CLIENTOUTPUT_DIR2 = ./output/outputTest2
FLAG_INCLUDE_DIR = -I ./includes/
SOCKNAME = ./mysock.sk
TARGETS = server client 
VALGRIND_OPTIONS = valgrind --leak-check=full

SERVER_DEPS = server.c util.c queue.c


CLIENT_DEPS = client.c util.c queue.c Parser.c


CLIENTCONFIGOPTIONS = -f $(SOCKNAME) -p -t 200

.PHONY : all, cleanall, test1, test2

all:
	make -B server
	make -B client

cleanall:
	rm -rf $(SOCKNAME) $(TARGETS)
	#rm $(CLIENTOUTPUT_DIR1)/*
	#rmdir $(CLIENTOUTPUT_DIR1)
	#rm $(CLIENTOUTPUT_DIR2)/*
	#rmdir $(CLIENTOUTPUT_DIR2)

#targets per generare gli eseguibili
server: $(SERVER_DEPS)
		#make cleanall
		$(CC) $(CFLAGS) $(SERVER_DEPS) -o server $(FLAG_INCLUDE_DIR)
				   

client: $(CLIENT_DEPS)
		#make cleanall
		$(CC) $(CFLAGS) $(CLIENT_DEPS) -o client $(FLAG_INCLUDE_DIR)
test1:
	chmod +x ./Test/TestScript1.sh
	./Test/TestScript1.sh $(CLIENTOUTPUT_DIR1)

test2:
	chmod +x ./Test/TestScript2.sh
	./Test/TestScript2.sh $(CLIENTOUTPUT_DIR2)