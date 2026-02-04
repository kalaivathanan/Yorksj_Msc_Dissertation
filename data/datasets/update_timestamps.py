import pandas as pd
from datetime import datetime, timedelta

def update_csv_timestamps(input_file, output_file, start_date, interval_seconds):
    """
    Update CSV timestamps to match your requirements
    
    Parameters:
    - input_file: Original CSV file path
    - output_file: Output CSV file path
    - start_date: New start date (e.g., "2026-01-19 00:00:00")
    - interval_seconds: Time between readings (30, 45, 60, etc.)
    """
    
    # Read CSV
    print(f"📖 Reading {input_file}...")
    df = pd.read_csv(input_file)
    
    # Generate new timestamps
    print(f"⏰ Generating new timestamps...")
    start = datetime.strptime(start_date, "%Y-%m-%d %H:%M:%S")
    new_timestamps = []
    
    for i in range(len(df)):
        new_time = start + timedelta(seconds=i * interval_seconds)
        new_timestamps.append(new_time.strftime("%Y-%m-%d %H:%M:%S"))
    
    # Update timestamp column
    df['timestamp'] = new_timestamps
    
    # Save updated CSV
    df.to_csv(output_file, index=False)
    
    # Print summary
    print(f"\n✅ CSV updated successfully!")
    print(f"   Input file:  {input_file}")
    print(f"   Output file: {output_file}")
    print(f"   Start date:  {new_timestamps[0]}")
    print(f"   End date:    {new_timestamps[-1]}")
    print(f"   Interval:    {interval_seconds} seconds")
    print(f"   Total rows:  {len(df)}")
    
    # Calculate duration
    total_seconds = len(df) * interval_seconds
    hours = total_seconds // 3600
    minutes = (total_seconds % 3600) // 60
    print(f"   Duration:    {hours}h {minutes}m")


# ============================================
# CONFIGURATION - CHANGE THESE VALUES
# ============================================

# FOR IRRIGATION SYSTEM (60-second intervals)
update_csv_timestamps(
    input_file="irrigation_dataset.csv",
    output_file="irrigation_2026_60s.csv",
    start_date="2026-01-27 12:40:00",  # Change to your desired start
    interval_seconds=60  # 60 seconds = 1 minute
)

# FOR FIRE DETECTION (30-second intervals)
update_csv_timestamps(
    input_file="fire_detection_dataset.csv",
    output_file="fire_detection_2026_30s.csv",
    start_date="2026-01-27 12:40:00",
    interval_seconds=30  # 30 seconds
)

# FOR WATER QUALITY (45-second intervals)
update_csv_timestamps(
    input_file="water_quality_dataset.csv",
    output_file="water_quality_2026_45s.csv",
    start_date="2026-01-27 12:40:00",
    interval_seconds=45  # 45 seconds
)

print("\n🎉 All datasets updated to 2026!")