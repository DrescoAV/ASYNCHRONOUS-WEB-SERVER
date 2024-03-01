ps -ef | grep aws | head -n 1 | cut -d' ' -f4 | xargs kill 
ps -ef | grep valgrind | head -n 2 | tail -n 1 | cut -d' ' -f4 | xargs kill 
pkill aws 