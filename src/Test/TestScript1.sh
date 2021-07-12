#!/bin/bash
# richiesto un parametro: nome della cartella in cui salvare gli output del client


if [ $# -lt 1 ];then
    echo 'Errore: parametro richiesto'
    exit
fi

saveDir=$1
commonConf="-f mysock.sk -t 200 -p"
#configurazioni di clients
confClient1="$commonConf -w .,3 "
confClient2="$commonConf -W Parser.c,queue.c"
confClient3="$commonConf -d $saveDir -r queue.c"
confClient4="$commonConf -d $saveDir -R 7"
confClient5="$commonConf -c Parser.c,queue.c -W ./FileTest/file1.txt,./FileTest/file3.txt,./FileTest/file2.txt"

#setup array di configurazioni client
clientConf[0]=$confClient1
clientConf[1]=$confClient2
clientConf[2]=$confClient3
clientConf[3]=$confClient4
clientConf[4]=$confClient5

#creazione shell in bg con valgrind per ottenerne il PID tramite $!
rm mysock.sk
valgrind --leak-check=full --show-leak-kinds=all ./server configs/config1.txt &
#./server configs/config1.txt &
serverPID=$!

#se la cartella già esiste, la cancello e la ricrerò tramite lo script
rm -rf $saveDir
#rmdir $saveDir

if mkdir $saveDir;then
    for((i=0;$i < ${#clientConf[@]};i++));do
        nomeFile=$saveDir/outputClient$i.txt
        if touch $nomeFile;then
        ./client ${clientConf[$i]} &> $nomeFile & #redirigo output client sul file
        else
          echo 'Errore creazione file'
        fi
        sleep 1 #intervallo di creazione dei clients
    done
    kill -s SIGHUP $serverPID #invio SIGHUP al server
    wait $serverPID
else
    echo 'Errore creazione directory'
fi