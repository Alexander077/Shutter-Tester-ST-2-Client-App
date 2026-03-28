import serial
import json
import time
import os
import sys
import base64
import msvcrt

PORT = 'COM40'
BAUD_RATE = 115200

# Enable ANSI escape sequences on Windows
os.system('')

def clear_screen():
    os.system('cls' if os.name == 'nt' else 'clear')

def input_with_discard(ser, prompt):
    """
    Non-blocking input function that continually reads and discards incoming
    serial data to prevent the ESP32 USB CDC buffer from filling up and blocking.
    """
    print(prompt, end="", flush=True)
    user_input = ""
    while True:
        if ser.in_waiting > 0:
            ser.read(ser.in_waiting)
            
        while msvcrt.kbhit():
            key = msvcrt.getch()
            if key in (b'\x00', b'\xe0'):
                msvcrt.getch() # Discard extended key code
                continue
            if key == b'\r':
                print()
                return user_input
            elif key == b'\x08':
                if len(user_input) > 0:
                    user_input = user_input[:-1]
                    sys.stdout.write("\b \b")
                    sys.stdout.flush()
            elif key == b'\x03': # Ctrl+C
                print("\n[Ctrl+C] Exiting...")
                sys.exit(0)
            else:
                char = key.decode('utf-8', errors='ignore')
                if char.isprintable():
                    user_input += char
                    sys.stdout.write(char)
                    sys.stdout.flush()
        time.sleep(0.01)

def wait_for_esc(ser):
    print("\nPress 'Esc' to return to main menu...")
    while True:
        if ser and ser.in_waiting > 0:
            ser.read(ser.in_waiting)
            
        if msvcrt.kbhit():
            key = msvcrt.getch()
            if key == b'\x1b': # Esc
                break
        time.sleep(0.01)

def send_and_receive(ser, payload, timeout=10.0):
    msg = json.dumps(payload) + '\n'
    print(f"\n[>>>] SENDING: {msg.strip()}")
    expected_cmd = payload.get("cmd")
    
    payload_bytes = msg.encode('utf-8')
    for i in range(0, len(payload_bytes), 32):
        ser.write(payload_bytes[i:i+32])
        ser.flush()
        time.sleep(0.02)
        
    end_time = time.time() + timeout
    while time.time() < end_time:
        if msvcrt.kbhit():
            key = msvcrt.getch()
            if key == b'\x1b':
                print("\n[!!!] Cancelled by user.")
                return None

        if ser.in_waiting > 0:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if not line:
                continue
            
            if line.startswith('{') and line.endswith('}'):
                try:
                    resp = json.loads(line)
                    if resp.get("cmd") == expected_cmd:
                        print(f"[<<<] RESPONSE: {line}")
                        time.sleep(0.1) 
                        return resp
                except json.JSONDecodeError:
                    print(f"[LOG] {line}")
            else:
                print(f"[LOG] {line}")
                
        time.sleep(0.01)

    print("[!!!] TIMEOUT: Device did not respond")
    return None

def test_light_setup(ser):
    clear_screen()
    print("=== LIGHT SETUP TEST ===")
    
    ser.reset_input_buffer()
    payload = {"cmd": "API_REQUEST_LIGHT_SETUP"}
    cmd_str = json.dumps(payload) + "\n"
    ser.write(cmd_str.encode('utf-8'))
    
    print("Waiting for light setup data. Press 'Esc' to exit...\n")
    
    lines_printed = 0
    try:
        while True:
            if msvcrt.kbhit():
                key = msvcrt.getch()
                if key == b'\x1b':
                    break
                    
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8', errors='replace').strip()
                if line:
                    try:
                        data = json.loads(line)
                        if data.get("cmd") == "API_REQUEST_LIGHT_SETUP":
                            formatted = json.dumps(data, indent=2)
                            
                            if lines_printed > 0:
                                sys.stdout.write(f"\033[{lines_printed}A\033[J")
                            
                            sys.stdout.write(formatted + "\n")
                            sys.stdout.flush()
                            lines_printed = len(formatted.split('\n'))
                    except json.JSONDecodeError:
                        pass
            else:
                time.sleep(0.01)
    except Exception as e:
        print(f"Error: {e}")

def test_measurement(ser):
    clear_screen()
    print("=== MEASUREMENT TEST ===")
    
    print("Select Frame Size:")
    print("0: 35mm")
    print("1: 6x45")
    print("2: 6x6")
    print("3: 6x7")
    sensor_index = input_with_discard(ser, "Enter sensor index (0-3): ").strip()
    if sensor_index not in ['0', '1', '2', '3']:
        print("\nInvalid choice.")
        wait_for_esc(ser)
        return
        
    print("\nSelect Curtain Movement:")
    print("0: Horizontal")
    print("1: Vertical")
    print("2: Leaf")
    curtain_movement = input_with_discard(ser, "Enter curtain movement (0-2): ").strip()
    if curtain_movement not in ['0', '1', '2']:
        print("\nInvalid choice.")
        wait_for_esc(ser)
        return
        
    payload = {
        "cmd": "API_REQUEST_MEASURE",
        "sensorIndex": int(sensor_index),
        "curtainMovement": int(curtain_movement)
    }
    
    ser.reset_input_buffer()
    cmd_str = json.dumps(payload) + "\n"
    print(f"\nSending command: {cmd_str.strip()}")
    ser.write(cmd_str.encode('utf-8'))
    
    print("\nWaiting for measurement results... Press 'Esc' to cancel.")
    
    result_received = False
    try:
        while True:
            if msvcrt.kbhit():
                key = msvcrt.getch()
                if key == b'\x1b':
                    if not result_received:
                        print("\nCancelled waiting for measurement.")
                    break
                    
            if not result_received and ser.in_waiting > 0:
                line = ser.readline().decode('utf-8', errors='replace').strip()
                if line:
                    try:
                        data = json.loads(line)
                        if data.get("cmd") == "API_REQUEST_MEASURE":
                            formatted = json.dumps(data, indent=2)
                            print("\n=== MEASUREMENT RESULT ===")
                            print(formatted)
                            print("==========================")
                            print("Press 'Esc' to return to main menu.")
                            result_received = True
                        else:
                            pass
                    except json.JSONDecodeError:
                        print(f"[LOG]: {line}")
            else:
                time.sleep(0.01)
    except Exception as e:
        print(f"Error: {e}")

def test_storage(ser):
    clear_screen()
    print("=== STORAGE API TEST ===")
    
    ser.reset_input_buffer()

    print("\n--- TEST 1: READ RECORDS LIST ---")
    send_and_receive(ser, {"cmd": "API_REQUEST_GET_RECORDS_LIST"})
    time.sleep(1)

    print("\n--- TEST 2: CREATE NEW RECORD ---")
    new_record_data = {
        "recordNumber": 0,
        "sensor0Time": 1.25,
        "sensor1Time": 1.30,
        "curtain1spanAspeed": 15.5,
        "curtain1spanAtime": 2.1,
        "slitWidthAverage": 3.14
    }
    res = send_and_receive(ser, {"cmd": "API_REQUEST_SAVE_RECORD", "record": new_record_data})
    
    created_id = None
    if res and res.get("status") == "API_RESPONSE_STATUS_OK":
        created_id = res.get("recordNumber")
        print(f"---> Success! Created record with ID: {created_id}")
    else:
        print("---> Error creating record. Aborting tests.")
        wait_for_esc(ser)
        return

    time.sleep(1)

    print(f"\n--- TEST 3: READ RECORD {created_id} ---")
    send_and_receive(ser, {"cmd": "API_REQUEST_GET_RECORD", "recordNumber": created_id})
    time.sleep(1)

    print(f"\n--- TEST 4: UPDATE RECORD {created_id} ---")
    update_record_data = new_record_data.copy()
    update_record_data["recordNumber"] = created_id
    update_record_data["sensor0Time"] = 99.99
    update_record_data["slitWidthAverage"] = 5.55
    send_and_receive(ser, {"cmd": "API_REQUEST_SAVE_RECORD", "record": update_record_data})
    time.sleep(1)

    print(f"\n--- TEST 5: VERIFY UPDATE ---")
    send_and_receive(ser, {"cmd": "API_REQUEST_GET_RECORD", "recordNumber": created_id})
    time.sleep(1)

    print(f"\n--- TEST 6: DELETE RECORD {created_id} ---")
    send_and_receive(ser, {"cmd": "API_REQUEST_DELETE_RECORD", "recordNumber": created_id})
    time.sleep(1)

    print(f"\n--- TEST 7: VERIFY DELETION ---")
    send_and_receive(ser, {"cmd": "API_REQUEST_GET_RECORD", "recordNumber": created_id})
    time.sleep(1)

    print("\n--- TEST 8: FINAL RECORDS LIST ---")
    send_and_receive(ser, {"cmd": "API_REQUEST_GET_RECORDS_LIST"})
    
    print("\nStorage API testing complete.")
    wait_for_esc(ser)

def test_firmware_update(ser):
    clear_screen()
    print("=== FIRMWARE UPDATE TEST ===")
    
    file_path = input_with_discard(ser, "Enter path to the encrypted .bin file (or drag & drop): ").strip()
    file_path = file_path.strip('"\'') 

    if not os.path.exists(file_path):
        print(f"\n[ERROR] File '{file_path}' not found.")
        wait_for_esc(ser)
        return

    chunk_size = 48
    file_size = os.path.getsize(file_path)
    
    print(f"\nPreparing for update...")
    print(f"File: {file_path}")
    print(f"Size: {file_size} bytes\n")

    ser.reset_input_buffer()

    print("Sending API_REQUEST_FIRMWARE_UPDATE command...")
    start_cmd = json.dumps({"cmd": "API_REQUEST_FIRMWARE_UPDATE"}) + '\n'
    ser.write(start_cmd.encode('ascii'))
    ser.flush()

    print("Waiting for FIRMWARE_UPDATE_READY from device...")
    is_ready = False
    timeout_ready = time.time() + 10.0
    
    while not is_ready and time.time() < timeout_ready:
        if msvcrt.kbhit():
            key = msvcrt.getch()
            if key == b'\x1b':
                print("\n[!!!] Cancelled by user.")
                wait_for_esc(ser)
                return

        if ser.in_waiting > 0:
            line = ser.readline().decode(errors='ignore').strip()
            if line:
                try:
                    resp = json.loads(line)
                    if resp.get("cmd") == "API_REQUEST_FIRMWARE_UPDATE":
                        status = resp.get("status")
                        if status == "API_RESPONSE_READY_FOR_FIRMWARE_UPDATE_DATA":
                            is_ready = True
                        elif status == "API_RESPONSE_STATUS_ERROR":
                            print(f"\n[DEVICE ERROR] {resp.get('message', 'Unknown initialization error')}")
                            wait_for_esc(ser)
                            return
                except json.JSONDecodeError:
                    print(f"[Device]: {line}")
        time.sleep(0.01)

    if not is_ready:
        print("\n[ERROR] Timeout waiting for ready signal.")
        wait_for_esc(ser)
        return

    print("\n[SYSTEM] Ready signal received! Starting data transfer. DO NOT DISCONNECT...")
    
    sent_bytes = 0
    start_time = time.time()
    ack_received = False
    
    time.sleep(0.1)

    try:
        with open(file_path, 'rb') as f:
            while True:
                chunk = f.read(chunk_size)
                if not chunk:
                    break
                
                b64_data = base64.b64encode(chunk).decode('ascii') + '\n'
                ser.write(b64_data.encode('ascii'))
                ser.flush()
                
                sent_bytes += len(chunk)
                
                ack_received = False
                chunk_timeout = time.time() + 2.0 
                
                while time.time() < chunk_timeout:
                    if msvcrt.kbhit():
                        key = msvcrt.getch()
                        if key == b'\x1b':
                            print("\n\n[!!!] Update cancelled by user.")
                            wait_for_esc(ser)
                            return

                    if ser.in_waiting > 0:
                        line = ser.readline().decode(errors='ignore').strip()
                        if line:
                            try:
                                resp = json.loads(line)
                                if resp.get("cmd") == "API_REQUEST_FIRMWARE_UPDATE":
                                    status = resp.get("status")
                                    
                                    if status == "API_RESPONSE_FIRMWARE_UPDATE_CHUNK_ACK":
                                        ack_bytes = int(resp.get("bytesReceived", 0))
                                        progress = (ack_bytes / file_size) * 100
                                        sys.stdout.write(f"\rProgress: [{ack_bytes}/{file_size} bytes] {progress:.1f}% ")
                                        sys.stdout.flush()
                                        
                                        if ack_bytes >= sent_bytes:
                                            ack_received = True
                                            break
                                            
                                    elif status == "API_RESPONSE_FIRMWARE_UPDATE_FAILED":
                                        print(f"\n\n[WRITE ERROR] {resp.get('message', 'Failed to write to flash')}")
                                        wait_for_esc(ser)
                                        return
                            except json.JSONDecodeError:
                                pass 
                    else:
                        time.sleep(0.005)
                        
                if not ack_received:
                    print(f"\n\n[ERROR] Timeout: Device did not acknowledge {sent_bytes} bytes!")
                    break

        if ack_received:
            total_time = time.time() - start_time
            print(f"\n\nTransfer completed in {total_time:.1f} sec.")
            print("Waiting for final status API_RESPONSE_FIRMWARE_UPDATE_SUCCESS...")
            
            timeout = time.time() + 15.0
            while time.time() < timeout:
                if ser.in_waiting > 0:
                    line = ser.readline().decode(errors='ignore').strip()
                    if line:
                        try:
                            resp = json.loads(line)
                            if resp.get("cmd") == "API_REQUEST_FIRMWARE_UPDATE":
                                status = resp.get("status")
                                if status == "API_RESPONSE_FIRMWARE_UPDATE_SUCCESS":
                                    print("\n[SUCCESS] Firmware uploaded successfully. Restart the device to finish the update.")
                                    wait_for_esc(ser)
                                    return
                                elif status == "API_RESPONSE_FIRMWARE_UPDATE_FAILED":
                                    print(f"\n[DEVICE ERROR] Error on final stage: {resp.get('message')}")
                                    wait_for_esc(ser)
                                    return
                        except json.JSONDecodeError:
                            print(f"[Device]: {line}")
                time.sleep(0.01)
            
            print("\n[WARNING] SUCCESS status not received, but data was sent. Check the device.")
    except Exception as e:
        print(f"\n[ERROR] Unexpected error during update: {e}")
        
    wait_for_esc(ser)

def test_echo(ser):
    clear_screen()
    print("=== ECHO API TEST ===")
    
    ser.reset_input_buffer()
    send_and_receive(ser, {"cmd": "API_REQUEST_ECHO"})
    
    wait_for_esc(ser)

def main():
    global PORT
    user_port = input(f"Enter COM port (default {PORT}): ").strip()
    if user_port:
        PORT = user_port

    print(f"Connecting to {PORT} at {BAUD_RATE}...")
    try:
        ser = serial.Serial(PORT, BAUD_RATE, timeout=0.1)
        ser.setDTR(False)
        ser.setRTS(False)
    except serial.SerialException as e:
        print(f"[ERROR] Failed to open COM port: {e}")
        return

    time.sleep(2)
    ser.reset_input_buffer()

    while True:
        clear_screen()
        print("=== ESP32-S2 SHUTTER TESTER API TESTING ===")
        print("1. Test Light Setup API")
        print("2. Test Measurement API")
        print("3. Test Records Storage API")
        print("4. Test Firmware Update API")
        print("5. Test Echo API")
        print("6. Exit")
        
        print("\nSelect an option (1-6): ", end="", flush=True)
        
        choice = None
        while choice is None:
            if ser.in_waiting > 0:
                ser.read(ser.in_waiting)
                
            if msvcrt.kbhit():
                key = msvcrt.getch().decode('utf-8', errors='ignore')
                if key in ['1', '2', '3', '4', '5', '6']:
                    choice = key
            time.sleep(0.01)
            
        if choice == '1':
            test_light_setup(ser)
        elif choice == '2':
            test_measurement(ser)
        elif choice == '3':
            test_storage(ser)
        elif choice == '4':
            test_firmware_update(ser)
        elif choice == '5':
            test_echo(ser)
        elif choice == '6':
            break

    ser.close()
    print("\nExited. Port closed.")

if __name__ == "__main__":
    main()
