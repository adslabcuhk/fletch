import re

def calculate_rx_ratio(file_path):
    total_rx = 0
    pattern = r"port 5/0 FRAMES RX = (\d+)"
    
    try:
        with open(file_path, 'r') as f:
            content = f.read()
            matches = re.findall(pattern, content)
            rx_values = [int(val) for val in matches]
            total_rx = sum(rx_values)
            
            if not rx_values:
                print("not find any matching lines in the log file.")
                return

            result = total_rx / 32000000
            
            print(f"num_servers: {len(rx_values)}, total_recir: {total_rx}, avg_recir: {result:.9f}")
            
    except FileNotFoundError:
        print(f"not found{file_path}")

workloads = ["Alibaba", "Training", "Thumb", "LinkedIn"]
num_servers = [16, 128]
for i in range(4):
    for j in range(2):
        print(f"workload: {workloads[i]}")
        calculate_rx_ratio(f'/home/jz/In-Switch-FS-Metadata/netfetch-private/mdtestcpp/results/round_recir_counts_r5_{num_servers[j]}/{i}/netfetch_0.9_32000000_10_4_3/running_status.log')
        