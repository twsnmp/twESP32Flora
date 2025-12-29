package main

import (
	"bufio"
	"flag"
	"fmt"
	"io"
	"log"
	"os"
	"os/exec"
	"strings"
	"time"

	"go.bug.st/serial"
	"go.bug.st/serial/enumerator"
)

var version = "vx.x.x"
var commit = ""

var esptool = ""
var serialPort = ""
var ssid = ""
var password = ""
var mqttIP = ""
var mqttPort = 0
var interval = 60
var sensorType = "DHT22" // BME280
var hasRainSensor = false

var mode = &serial.Mode{
	BaudRate: 115200,
	InitialStatusBits: &serial.ModemOutputBits{
		RTS: true,
		DTR: true,
	},
}

func init() {
	flag.StringVar(&esptool, "esptool", "", "path to esptool")
	flag.StringVar(&serialPort, "port", "", "serial port name")
	flag.StringVar(&ssid, "ssid", "", "wifi ssid")
	flag.StringVar(&password, "password", "", "wifi password")
	flag.StringVar(&mqttIP, "mqttIP", "", "MQTT Broker IP address")
	flag.IntVar(&mqttPort, "mqttPort", 1883, "MQTT Broker port")
	flag.IntVar(&interval, "interval", 60, "MQTT send interval(sec)")
	flag.StringVar(&sensorType, "sensor", "DHT22", "Temperature and Humidity sensor type(DHT22 | BME280)")
	flag.BoolVar(&hasRainSensor, "rain", false, "Has rain sensor")
	flag.Parse()
	flag.Usage = func() {
		fmt.Fprintf(os.Stderr, "Usage: %s [options...] command\n%s", os.Args[0],
			`command
			      list : list serial ports  monitor : monitor serial port
  config : config ESP32
  write : write firmware to ESP32
  clear : clear config
  reset : reset ESP32
  version : show version
options
`)
		flag.PrintDefaults()
	}
}

func main() {
	cmd := flag.Arg(0)
	log.Printf("serial=%s", serialPort)
	switch cmd {
	case "list":
		listSerialPort()
	case "monitor":
		if err := monitorESP32(); err != nil {
			log.Fatalln(err)
		}
	case "config":
		if err := configESP32(); err != nil {
			log.Fatalln(err)
		}
	case "write":
		if err := writeESP32(); err != nil {
			log.Fatalln(err)
		}
	case "reset":
		if err := resetESP32(); err != nil {
			log.Fatalln(err)
		}
	case "version":
		fmt.Printf("twESP32FloraCLI %s(%s)\n", version, commit)
	default:
		flag.Usage()
	}
}

func listSerialPort() {
	ports, err := enumerator.GetDetailedPortsList()
	if err != nil {
		log.Fatal(err)
	}
	for _, port := range ports {
		if port.IsUSB {
			fmt.Printf("%s (%s/%s:%s)\n", port.Name, port.VID, port.PID, port.SerialNumber)
		}
	}
}

// monitor ESP32 form serial port
func monitorESP32() error {
	p, err := serial.Open(serialPort, mode)
	if err != nil {
		log.Printf("serial=%s mode=%+v", serialPort, mode)
		return err
	}
	defer p.Close()
	for {
		line, err := readLine(p)
		if err != nil {
			return err
		}
		fmt.Println(line)
	}
}

// config ESP32 form serial port
func configESP32() error {
	if ssid == "" {
		return fmt.Errorf("no ssid")
	}
	if mqttIP == "" {
		return fmt.Errorf("no mqttIP")
	}
	p, err := serial.Open(serialPort, mode)
	if err != nil {
		return err
	}
	defer p.Close()

	// Reset ESP32 to trigger config mode
	p.SetDTR(false)
	time.Sleep(time.Millisecond * 500)
	p.SetDTR(true)
	time.Sleep(time.Millisecond * 1000)

	// Set a longer read timeout to wait for user input on CLI
	p.SetReadTimeout(time.Second * 60)
	gotStart := false
	for {
		line, err := readLine(p)
		if err != nil {
			// io.EOF can happen if the port is closed or device reboots.
			if err == io.EOF {
				log.Println("Readline EOF, assuming config finished or device disconnected.")
				return nil
			}
			return err
		}
		fmt.Println(line)
		if !gotStart {
			// sync
			if strings.HasPrefix(line, "setup start") {
				gotStart = true
			}
			continue
		}
		time.Sleep(time.Millisecond * 100)
		switch {
		case strings.HasPrefix(line, "enter ssid:"):
			p.Write([]byte(ssid + "\n"))
		case strings.HasPrefix(line, "enter password:"):
			p.Write([]byte(password + "\n"))
		case strings.HasPrefix(line, "enter mqtt ip:"):
			p.Write([]byte(mqttIP + "\n"))
		case strings.HasPrefix(line, "enter mqtt port"):
			p.Write([]byte(fmt.Sprintf("%d\n", mqttPort)))
		case strings.HasPrefix(line, "enter monitor interval"):
			p.Write([]byte(fmt.Sprintf("%d\n", interval)))
		case strings.HasPrefix(line, "enter sensor type"):
			p.Write([]byte(sensorType + "\n"))
		case strings.HasPrefix(line, "Has rain sensor?"):
			if hasRainSensor {
				p.Write([]byte("yes\n"))
			} else {
				p.Write([]byte("no\n"))
			}
		case strings.HasPrefix(line, "Prepare for calibration"):
			fmt.Println("ACTION: Remove the soil moisture sensor from the soil and dry it. Then press the <Enter> key.")
			bufio.NewReader(os.Stdin).ReadString('\n')
			p.Write([]byte("\n"))
		case strings.HasPrefix(line, "Place the soil moisture sensor in water"):
			fmt.Println("ACTION: Place the soil moisture sensor in water, then press the <Enter> key.")
			bufio.NewReader(os.Stdin).ReadString('\n')
			p.Write([]byte("\n"))
		case strings.HasPrefix(line, "Dry the rain sensor"):
			fmt.Println("ACTION: Dry the rain sensor, then press the <Enter> key.")
			bufio.NewReader(os.Stdin).ReadString('\n')
			p.Write([]byte("\n"))
		case strings.HasPrefix(line, "Drop water on the rain sensor"):
			fmt.Println("ACTION: Drop water on the rain sensor, then press the <Enter> key.")
			bufio.NewReader(os.Stdin).ReadString('\n')
			p.Write([]byte("\n"))
		case strings.HasPrefix(line, "Config ssid="):
			log.Println("Config successful.")
			return nil
		}
	}
}

func resetESP32() error {
	p, err := serial.Open(serialPort, mode)
	if err != nil {
		return err
	}
	defer p.Close()
	// Reset ESP32
	p.SetDTR(false)
	time.Sleep(time.Millisecond * 100)
	p.SetDTR(true)
	return nil
}

func readLine(p serial.Port) (string, error) {
	buff := make([]byte, 1)
	r := ""
	for {
		n, err := p.Read(buff)
		if err != nil {
			return "", err
		}
		if n == 0 {
			return r, nil
		}
		if buff[0] == 0x0a || buff[0] == 0x0d {
			if r != "" {
				return r, nil
			}
			continue
		}
		r += string(buff[:n])
	}
}

// write firmware to ESP32
func writeESP32() error {
	if esptool == "" {
		esptool = findESPTool()
		if esptool == "" {
			return fmt.Errorf("esptool not found")
		}
	}
	name := esptool
	args := []string{}
	if strings.HasSuffix(esptool, ".py") {
		if p, err := exec.LookPath("python"); err == nil {
			name = p
		} else {
			if p, err := exec.LookPath("python3"); err == nil {
				name = p
			} else {
				return fmt.Errorf("python not found")
			}
		}
		args = append(args, esptool)
	}
	/*
	   For XIAO ESP32C3
	   --chip esp32c3 --port <serial port> --baud 921600
	   --before default-reset --after hard-reset write-flash  -z --flash-mode dio
	   --flash-freq 80m --flash-size 4MB
	   0x0 ./twESP32Flora.ino.bootloader.bin
	   0x8000 ./twESP32Flora.ino.partitions.bin
	   0xe000 ./boot_app0.bin
	   0x10000 ./twESP32Flora.ino.bin
	   Note: The binary file and address may need to be adjusted for the ESP32C3.
	*/

	args = append(args, "--chip")
	args = append(args, "esp32c3")
	args = append(args, "--port")
	args = append(args, serialPort)
	args = append(args, "--baud")
	args = append(args, "921600")
	args = append(args, "--before")
	args = append(args, "default-reset")
	args = append(args, "--after")
	args = append(args, "hard-reset")
	args = append(args, "write-flash")
	args = append(args, "-z")
	args = append(args, "--flash-mode")
	args = append(args, "dio")
	args = append(args, "--flash-freq")
	args = append(args, "80m")
	args = append(args, "--flash-size")
	args = append(args, "4MB")
	args = append(args, "0x0")
	args = append(args, "./twESP32Flora.ino.bootloader.bin")
	args = append(args, "0x8000")
	args = append(args, "./twESP32Flora.ino.partitions.bin")
	args = append(args, "0xe000")
	args = append(args, "./boot_app0.bin")
	args = append(args, "0x10000")
	args = append(args, "./twESP32Flora.ino.bin")
	log.Println(name, args)
	cmd := exec.Command(name, args...)
	stdout, err := cmd.StdoutPipe()
	if err != nil {
		return err
	}
	stderr, err := cmd.StderrPipe()
	if err != nil {
		return err
	}
	err = cmd.Start()
	if err != nil {
		return err
	}
	go dumpOutput(stdout)
	go dumpOutput(stderr)
	cmd.Wait()
	return nil
}

// dumpOutput show stdout and stderr
func dumpOutput(r io.ReadCloser) {
	buff := make([]byte, 1024)
	for {
		n, err := r.Read(buff)
		if err != nil {
			return
		}
		if n > 0 {
			fmt.Print(string(buff[:n]))
		}
	}
}

func findESPTool() string {
	if p, err := exec.LookPath("esptool"); err == nil {
		return p
	}
	if p, err := exec.LookPath("./esptool"); err == nil {
		return p
	}
	return ""
}
