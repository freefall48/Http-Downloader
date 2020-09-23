import time
import csv
import subprocess

file_sizes = ["0.1", "1", "5", "10", "50", "100"]

with open('data3.csv', 'w', newline='') as data_file:
    writer = csv.writer(data_file)
    # Test up to 32 threads. Any higher is pointless.
    print("Testing large.txt")
    for i in range(1, 32 + 1):
        # # Do 3 runs for each thread count 
        
        for x in range(0, 3):
            print("Run {} for {} threads".format(x, i))
            start = time.time_ns()
            subprocess.call("./bin/downloader download_urls/large.txt " + str(i) + " files", shell=True, stdout=subprocess.DEVNULL)
            end = time.time_ns()
            time_taken = end - start
            writer.writerow(['large', i, time_taken])

    print("Testing small.txt")
    for i in range(1, 32 + 1):
        # # Do 3 runs for each thread count 
        for x in range(0, 3):
            print("Run {} for {} threads".format(x, i))
            start = time.time_ns()
            subprocess.call("./bin/downloader download_urls/small.txt " + str(i) + " files", shell=True, stdout=subprocess.DEVNULL)
            end = time.time_ns()
            time_taken = end - start
            writer.writerow(['small', i, time_taken])
    # ix = [1, 2, 4, 8, 16, 32]
    # for i in ix:
    #     for size in file_sizes:
    #         print("Testing {}mb.txt".format(size))
    #         start = time.time_ns()
    #         subprocess.run(["./downloader", "download_urls/{}mb.txt".format(size), "{}".format(i), "files"])
    #         end = time.time_ns()
    #         time_taken = end - start
    #         writer.writerow([size, i, time_taken])