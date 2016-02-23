#!/bin/bash

touch output2
./dht 10 1000 1 >> output2
./dht 10 1000 10 >> output2
./dht 10 1000 100 >> output2
./dht 10 1000 1000 >> output2
./dht 10 1000 10000 >> output2
./dht 10 1000 100000 >> output2
./dht 10 1000 1000000 >> output2
