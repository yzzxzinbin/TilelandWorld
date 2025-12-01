import re
import csv
import os

# Define log file path (adjust if needed)
log_file_path = r"k:\test program\TilelandWorld\build\tui_test.log"  # Assuming the log file is tui_test.log

# Define output CSV path in devtool
csv_file_path = r"k:\test program\TilelandWorld\devtool\render_times.csv"

# Regex pattern to match render time lines
pattern = re.compile(r"\[WARN\] Frame (\d+) lag: (\d+\.\d+) ms")


def extract_render_times():
    render_data = []

    if not os.path.exists(log_file_path):
        print(f"Log file not found: {log_file_path}")
        return

    with open(log_file_path, "r", encoding="utf-8") as file:
        for line in file:
            match = pattern.search(line)
            if match:
                frame = int(match.group(1))
                ticks = float(match.group(2))
                render_data.append((frame, ticks))

    # Write to CSV
    with open(csv_file_path, "w", newline="", encoding="utf-8") as csvfile:
        writer = csv.writer(csvfile)
        writer.writerow(["frame", "ticks"])  # Header
        writer.writerows(render_data)

    print(f"Extracted {len(render_data)} render time entries to {csv_file_path}")


if __name__ == "__main__":
    extract_render_times()
