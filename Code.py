import time
import random

def measure_air_quality():
    # Simulating air quality measurement
    air_quality = random.uniform(0, 100)
    return air_quality

def monitor_air_quality():
    while True:
        air_quality = measure_air_quality()
        print("Air Quality Index: {:.2f}".format(air_quality))
        time.sleep(1)  # Wait for 1 second

if __name__ == "__main__":
    monitor_air_quality()
