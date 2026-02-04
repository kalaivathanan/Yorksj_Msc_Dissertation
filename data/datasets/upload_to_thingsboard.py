import pandas as pd
import requests
import json
import time
from datetime import datetime

def upload_csv_to_thingsboard(csv_file, device_token, thingsboard_host="demo.thingsboard.io", delay_seconds=0.5):
    """
    Upload CSV dataset to ThingsBoard device
    
    Parameters:
    - csv_file: Path to CSV file
    - device_token: Device access token from ThingsBoard
    - thingsboard_host: ThingsBoard server (default: demo.thingsboard.io)
    - delay_seconds: Delay between uploads (0.5 = half second)
    """
    
    print(f"\n{'='*60}")
    print(f"UPLOADING: {csv_file}")
    print(f"{'='*60}")
    
    # Read CSV
    try:
        df = pd.read_csv(csv_file)
        print(f"CSV loaded: {len(df)} rows")
    except FileNotFoundError:
        print(f"ERROR: File '{csv_file}' not found!")
        return
    
    # Build URL
    url = f"http://{thingsboard_host}/api/v1/{device_token}/telemetry"
    
    # Upload statistics
    success_count = 0
    fail_count = 0
    start_time = time.time()
    
    print(f"\n Starting upload...")
    print(f"   Server: {thingsboard_host}")
    print(f"   Delay: {delay_seconds}s between uploads")
    print(f"   Estimated time: {len(df) * delay_seconds / 60:.1f} minutes\n")
    
    # Upload each row
    for index, row in df.iterrows():
        # Build telemetry payload
        telemetry = {}
        
        for column in df.columns:
            if column != 'timestamp' and column != 'smoke_detected':
                # Convert to float, handle NaN
                value = row[column]
                telemetry[column] = float(value) if pd.notna(value) else 0
            elif column == 'smoke_detected':
                # Keep as integer (0 or 1)
                telemetry[column] = int(row[column]) if pd.notna(row[column]) else 0
        
        # Add timestamp if available
        if 'timestamp' in df.columns:
            try:
                timestamp_ms = int(pd.to_datetime(row['timestamp']).timestamp() * 1000)
                payload = {"ts": timestamp_ms, "values": telemetry}
            except:
                payload = telemetry
        else:
            payload = telemetry
        
        # Send to ThingsBoard
        try:
            response = requests.post(
                url, 
                data=json.dumps(payload),
                headers={'Content-Type': 'application/json'},
                timeout=10
            )
            
            if response.status_code == 200:
                success_count += 1
                # Print progress every 50 rows
                if (index + 1) % 50 == 0 or index == 0:
                    print(f"    Progress: {index+1}/{len(df)} rows uploaded")
            else:
                fail_count += 1
                print(f"   Row {index+1} failed: {response.status_code} - {response.text}")
                
        except requests.exceptions.RequestException as e:
            fail_count += 1
            print(f"   Row {index+1} error: {str(e)}")
        
        # Delay before next upload
        if index < len(df) - 1:
            time.sleep(delay_seconds)
    
    # Final summary
    elapsed_time = time.time() - start_time
    print(f"\n{'='*60}")
    print(f" UPLOAD SUMMARY")
    print(f"{'='*60}")
    print(f"   Successful: {success_count}/{len(df)}")
    print(f"   Failed: {fail_count}/{len(df)}")
    print(f"   Time taken: {elapsed_time/60:.1f} minutes")
    print(f"{'='*60}\n")


# ============================================
# CONFIGURATION
# ============================================

# ThingsBoard Server
THINGSBOARD_HOST = "demo.thingsboard.io" 

# Device Tokens (GET THESE FROM THINGSBOARD!)
IRRIGATION_TOKEN = "m8Ow9HPi3U5uJ3JrGtde"
FIRE_TOKEN = "nFkkubhRKKNSoDlrKpHh"  
WATER_QUALITY_TOKEN = "YxN8CYTzWPl32TXg33PH"

# CSV Files (updated with 2026 timestamps)
IRRIGATION_CSV = "irrigation_2026_60s.csv"
FIRE_CSV = "fire_detection_2026_30s.csv"
WATER_QUALITY_CSV = "water_quality_2026_45s.csv"

# Upload delay (seconds between each row)
UPLOAD_DELAY = 0.5 


# ============================================
# CHOOSE WHICH DATASET TO UPLOAD
# ============================================

print("\n" + "="*60)
print(" THINGSBOARD DATASET UPLOADER")
print("="*60)
print("\nWhich dataset do you want to upload?")
print("  1. Irrigation System")
print("  2. Fire Detection")
print("  3. Water Quality")
print("  4. ALL THREE (one after another)")
print("  0. Exit")

choice = input("\nEnter your choice (0-4): ")

if choice == "1":
    upload_csv_to_thingsboard(IRRIGATION_CSV, IRRIGATION_TOKEN, THINGSBOARD_HOST, UPLOAD_DELAY)

elif choice == "2":
    upload_csv_to_thingsboard(FIRE_CSV, FIRE_TOKEN, THINGSBOARD_HOST, UPLOAD_DELAY)

elif choice == "3":
    upload_csv_to_thingsboard(WATER_QUALITY_CSV, WATER_QUALITY_TOKEN, THINGSBOARD_HOST, UPLOAD_DELAY)

elif choice == "4":
    print("\n Uploading all three datasets...")
    upload_csv_to_thingsboard(IRRIGATION_CSV, IRRIGATION_TOKEN, THINGSBOARD_HOST, UPLOAD_DELAY)
    upload_csv_to_thingsboard(FIRE_CSV, FIRE_TOKEN, THINGSBOARD_HOST, UPLOAD_DELAY)
    upload_csv_to_thingsboard(WATER_QUALITY_CSV, WATER_QUALITY_TOKEN, THINGSBOARD_HOST, UPLOAD_DELAY)

elif choice == "0":
    print("\n Goodbye!")

else:
    print("\n Invalid choice!")

print("\n Script finished!\n")