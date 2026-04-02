import threading
import queue
import socket
import time
import random

#Simple HTTP no URL needed, just IP and Port

SERVER_IP= '127.0.0.1'
SERVER_PORT= 5980

# Configure the number of threads and operations
NUM_THREADS = 3
OPS_PER_THREAD = 100000
PRINT_INTERVAL = 3  # Interval for printing intermediate results

# Queues for managing operations and latencies
operations_queue = queue.Queue()
latencies_queue = queue.Queue()

# Synchronize the starting of threads
start_event = threading.Event()

# Client operation function
def kv_store_operation(op_type, key, value=None):
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
            s.settimeout(2.0)
            s.connect((SERVER_IP, SERVER_PORT))
            if op_type == 'set':
                message = f"PUT {key} {value}"

            elif op_type == 'get':
                message = f"GET {key}"

            elif op_type== 'del':
                message= f"DEL {key}"
            else:
                raise ValueError("Invalid operation type")
            
            s.sendall(message.encode()) #Sends our commands to our kv_Store
            response = s.recv(1024).decode() #Servers reply
            return True
    except Exception as e:
        print(f"Error during {op_type} operation for key '{key}': {e}")
        return False

# Worker thread function
def worker_thread():
    while not start_event.is_set():
        # Wait until all threads are ready to start
        pass

    while not operations_queue.empty():
        op, key, value = operations_queue.get()
        start_time = time.time()
        if kv_store_operation(op, key, value):
            latency = time.time() - start_time
            latencies_queue.put(latency)

# Monitoring thread function
def monitor_performance():
    last_print = time.time()
    while True:
        time.sleep(PRINT_INTERVAL)
        current_time = time.time()
        elapsed_time = current_time - last_print
        latencies = []
        while not latencies_queue.empty():
            latencies.append(latencies_queue.get())

        if latencies:
            avg_latency = sum(latencies) / len(latencies)
            throughput = len(latencies) / elapsed_time
            print(f"[Last {PRINT_INTERVAL} seconds] Throughput: {throughput:.2f} ops/sec, "
                  f"Avg Latency: {avg_latency:.5f} sec/ops")
        last_print = time.time()

# Populate the operation queue with mixed 'set' and 'get' requests
for i in range(NUM_THREADS * OPS_PER_THREAD):  
    cmdType = i % 3  # Tests DEL also
    if cmdType == 0: op_type = 'set'
    elif cmdType == 1: op_type = 'get'
    else: op_type = 'del'
    key = f"key_{i}"
    value = f"value_{i}" if op_type == 'set' else None
    operations_queue.put((op_type, key, value))

# Create and start worker threads
threads = [threading.Thread(target=worker_thread) for _ in range(NUM_THREADS)]

# Start the monitoring thread
monitoring_thread = threading.Thread(target=monitor_performance, daemon=True)
monitoring_thread.start()

# Starting benchmark
start_time = time.time()
start_event.set()  # Signal threads to start

for thread in threads:
    thread.start()

for thread in threads:
    thread.join()

# Calculate final results
total_time = time.time() - start_time
total_ops = NUM_THREADS * OPS_PER_THREAD * 2  # times two for 'set' and 'get'
total_latencies = list(latencies_queue.queue)
average_latency = sum(total_latencies) / len(total_latencies) if total_latencies else float('nan')
throughput = total_ops / total_time

print("\nFinal Results:")
print(f"Total operations: {total_ops}")
print(f"Total time: {total_time:.2f} seconds")
print(f"Throughput: {throughput:.2f} operations per second")
print(f"Average Latency: {average_latency:.5f} seconds per operation")
