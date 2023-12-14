import serial.tools.list_ports
import serial
import matplotlib.pyplot as plt
import time


class DataInterface:
    # Indexing keys
    parameters = {
        "Thermocouple": 0,
        "Ambient": 1,
        "Run Time": 2,
        "State Time": 3,
        "Reflow State": 4,
    }
    # Instantiate an object of the serial class
    serial_line = serial.Serial(port=None)
    # Check if connection to the serial port was successful
    connection_status = True
    # Reject the initial stream of data sent by the microcontroller
    filtering = True

    @staticmethod
    def list_ports():
        print("Available Ports:")
        for port in serial.tools.list_ports.comports():
            print(port)
        print()

    def attempt_connection(self):
        com_select = input("Port Select: ")
        for port in serial.tools.list_ports.comports():
            if com_select in port:
                # Convert to string object which is needed for serial constructor
                com_select = str(port)
                self.serial_line = serial.Serial(com_select[0:(com_select.index('-') - 1)], 9600)
                self.serial_line.timeout = 5
                # Confirm that the serial line is open for reading
                if self.serial_line.isOpen():
                    print("Connected to " + com_select)
                else:
                    self.serial_line.open()
                    print("Connected to " + com_select)
                break
        else:
            # If specified port is not found in the existing list
            print("Unavailable Port")
            self.connection_status = False


class DynamicGraph:
    # Minimum value on the temperature axis
    __min_y = 0
    # Maximum value on the temperature axis
    __max_y = 450

    def __init__(self):
        # Clear data log file on startup
        open("datalog.txt", "w").close()
        # Setting up the graph
        self.figure, self.ax = plt.subplots()
        self.lines, = self.ax.plot([], [], ':')
        self.ax.set_title("Reflow in Progress")
        self.ax.set_xlabel("Time")
        self.ax.set_ylabel("Oven Temperature")
        # Auto scale must be enabled on the time axis since the time range is variable
        self.ax.set_autoscalex_on(True)
        self.ax.set_ylim([self.__min_y, self.__max_y])
        self.ax.grid()

    def __super_loop(self, xdata, ydata):
        # Updates to the existing data with data from the serial line
        self.lines.set_xdata(xdata)
        self.lines.set_ydata(ydata)
        # Rescaling
        self.ax.relim()
        self.ax.autoscale_view()

        self.figure.canvas.draw()
        self.figure.canvas.flush_events()

    def __call__(self, interface_object):
        if interface_object.connection_status is True:
            # Enable interactive mode
            plt.ion()
            self.__init__()
            xdata = []
            ydata = []

            while True:
                if interface_object.serial_line.in_waiting and not interface_object.filtering:
                    interface_object.serial_line.flushInput()
                    while interface_object.serial_line.read().decode('ascii') != '~':
                        pass
                    serial_data = interface_object.serial_line.readline().decode('ascii').strip('\n').split()
                    with open("datalog.txt", "a") as datalog:
                        print(*serial_data, sep=" ", file=datalog)
                    xdata.append(int(serial_data[interface_object.parameters["Run Time"]]))
                    ydata.append(int(serial_data[interface_object.parameters["Thermocouple"]]))
                    self.__super_loop(xdata, ydata)
                    serial_data.clear()
                else:
                    interface_object.filtering = False
                time.sleep(1)


interface = DataInterface()
interface.list_ports()
interface.attempt_connection()
plot = DynamicGraph()
plot(interface)
