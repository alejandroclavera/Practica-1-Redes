#!/usr/local/bin/python3.7
import datetime
import select
import sys
import socket
import struct
import time
import threading
from enum import Enum

DEFAULT_CONFIGURATION = 'client.cfg'

class PackageType(Enum):
   REG_REQ = 0X00
   REG_INFO = 0X01
   REG_ACK = 0X02
   INFO_ACK = 0X03
   REG_NACK = 0X04
   INFO_NACK = 0X05
   REG_REJ = 0X06
   ALIVE = 0x10
   ALIVE_REJ = 0x11

class PackageDataType(Enum):
   SEND_DATA = 0x20
   SET_DATA = 0x21
   GET_DATA = 0x22
   DATA_ACK = 0x23
   DATA_NACK = 0x24
   DATA_REJ = 0x25

class ClientState(Enum):
   DISCONNECTED = 0xa0
   NOT_REGISTERED = 0xa1
   WAIT_ACK_REG = 0xa2
   WAIT_INFO = 0xa3
   WAIT_ACK_INFO = 0xa4
   REGISTERED = 0xa5
   SEND_ALIVE = 0xa6

#Clase de la configuración
class Configuration():
   def __init__(self, configuration):
      self.id = configuration.get('Id')
      self.elements = configuration.get('Params')
      self.local_tcp = int(configuration.get('Local-TCP'))
      self.server = configuration.get('Server')
      self.server_udp = int(configuration.get('Server-UDP'))
      self.elements_state = {}
      for element in self.elements.split(';'):
         self.elements_state[element] = None

#Clase Paquete
class Package():  
   def __init__(self, package_type, id, random_number, data):
      self.package_type = package_type
      self.id = id
      self.random_number = random_number
      self.data = data

   def pack(self):
      package = struct.pack('!B', self.package_type) 
      package += bytes(self.id + '\0' + self.random_number + '\0' + self.data.ljust(61,'\0'), 'utf-8')
      return package

   @staticmethod
   def unpack(package, all_data_segment = False):
      package_type = package[0]
      package_id = package[1:13]
      package_random_number = package[14:22]
      package_data = package[23:]
      if not all_data_segment:
         package_data = package_data[:package_data.find(0)]
      return Package(package_type, package_id, package_random_number, package_data)
        
def load_configuration(configuration_file):
   config_file = open(configuration_file, 'r')
   configuration = {}
   for parametre in config_file:
      parametre = parametre.replace(' ','').split('=')
      parametre_nom = parametre[0]
      parametre_value = parametre[1].replace('\n','')
      configuration[parametre_nom] = parametre_value   
   return Configuration(configuration)
   
def setup():
   global configuration, socket_udp, socket_tcp, client_state
   configuration = load_configuration(DEFAULT_CONFIGURATION)
   socket_udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
   socket_tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
   client_state = ClientState.NOT_REGISTERED 

def check_package(package):
   return package.id == server_id and package.random_number == random_number

def send_wait_package(package, time_wait, next_state, port_udp = None):
   global register_state
   port_udp = port_udp if port_udp != None else configuration.server_udp
   socket_udp.sendto(package.pack(), (configuration.server, port_udp))
   register_state = next_state
   request = select.select([socket_udp], [], [], time_wait)
   return 0 if len(request[0]) != 0 else -1
   
def send_req_packages():
   global t_wait, register_state
   packetsSend = 0
   t_wait = 1
   q = 3
   p = 3
   multiplicator_t = 1
   while packetsSend < 7 :
         package = Package(PackageType.REG_REQ.value, configuration.id, ''.ljust(8,'0'), '')
         if send_wait_package(package, t_wait, ClientState.WAIT_ACK_REG) == -1:
            if multiplicator_t < p:
               p -= 1
            else:
               multiplicator_t += 1
               t_wait += t_wait if multiplicator_t < q else 1
            packetsSend += 1
         else :
            return 0
   return -1

def register_process():
   global server_id, random_number, register_state, t_wait, tcp_port
   end_process = False
   while not end_process:
      #Fase Envio paquete [REQ_REG]
      if send_req_packages() == -1:
         register_state = ClientState.NOT_REGISTERED
         return ClientState.NOT_REGISTERED   
      package, server_info = socket_udp.recvfrom(84)
      print(package)
      package = Package.unpack(package)   
      if package.package_type == PackageType.REG_REJ.value:
         register_state = ClientState.NOT_REGISTERED
         #notificamos que el servidor ha rechazado el registro
         return PackageType.REG_REJ
      elif package.package_type == PackageType.REG_NACK.value:
         register_state = ClientState.NOT_REGISTERED
         continue
      elif package.package_type == PackageType.REG_ACK.value:
         server_id = package.id
         #random_number = int(package.random_number)
         random_number = package.random_number
         udp_port = int(package.data) 
      else:
         register_state = ClientState.NOT_REGISTERED
         return -1
      #Fase de envio [REG_INFO]
      package = Package(PackageType.REG_INFO.value,configuration.id, random_number.decode(), str(configuration.local_tcp) + ','+ configuration.elements)
      if send_wait_package(package, t_wait * 2, ClientState.WAIT_ACK_INFO, port_udp= udp_port)== -1:
         register_state = ClientState.NOT_REGISTERED
         return -1
      else:
        package, server_info = socket_udp.recvfrom(84)
        package = Package.unpack(package)
      if check_package(package) and package.package_type == PackageType.INFO_NACK.value:
         register_state = ClientState.NOT_REGISTERED
         continue
      elif check_package(package) and package.package_type == PackageType.INFO_ACK.value:
         tcp_port = int(package.data)
      else:
         register_state = ClientState.NOT_REGISTERED
         print('Datos identificación incorrectos')
         return -1
      end_process = True
   register_state = ClientState.REGISTERED
   return ClientState.REGISTERED
      
def register_procediment():
   global register_state
   register_state = ClientState.NOT_REGISTERED
   n_process = 0
   while n_process < 3:
      process_state = register_process()
      if process_state == ClientState.NOT_REGISTERED:
         n_process += 1
         time.sleep(2)
      elif process_state == PackageType.REG_REJ:
         print('Error, el servidor ha rechazado el paquete de registro')
         print('Iniciando un nuevo proceso de registro')
         register_state = ClientState.NOT_REGISTERED
         n_process = 0
      elif process_state == -1:  #-1 -> iniciar nuevo proceso de registro
         register_state = ClientState.NOT_REGISTERED
         n_process = 0
      else:
         print('Proceso de registro realizado')
         return ClientState.REGISTERED
   print('No se ha podido registrarse en el servidor')
   return ClientState.DISCONNECTED

def send_alive():
   global socket_udp, client_state
   package = Package(PackageType.ALIVE.value, configuration.id, random_number.decode(), '')
   if client_state == ClientState.REGISTERED:
      socket_udp.sendto(package.pack(), (configuration.server, configuration.server_udp))
   else:
      while client_state == ClientState.SEND_ALIVE:
         socket_udp.sendto(package.pack(), (configuration.server, configuration.server_udp))
         time.sleep(2)

def wait_alive():
   global client_state, socket_udp
   packets_lost = 0
   if client_state == ClientState.REGISTERED:
      request = select.select([socket_udp], [], [], 4)
      if len(request[0]) == 0:
         client_state == ClientState.NOT_REGISTERED
      else:
         package, server_info= socket_udp.recvfrom(84)
         package = Package.unpack(package) 
         if not check_package(package):
            client_state = ClientState.NOT_REGISTERED
         else:
            client_state = ClientState.SEND_ALIVE
   else:
      while client_state == ClientState.SEND_ALIVE:
         request = select.select([socket_udp], [], [], 2)
         if len(request[0]) == 0:
            packets_lost += 1
         else:
            packets_lost = 0
            #client_state = ClientState.SEND_ALIVE
            package, server_info= socket_udp.recvfrom(84)
            package = Package.unpack(package)
            id_data = package.data.decode()
            if not check_package(package) or id_data != configuration.id:
               client_state = ClientState.NOT_REGISTERED
         if packets_lost >= 3:
            client_state = ClientState.NOT_REGISTERED
            print('\rSe perdio la conexion')        

# Apartado Comandos
def command_system():
   global client_state
   while client_state == ClientState.SEND_ALIVE:   
      print('\r->', end='')
      sys.stdin.flush()
      request = select.select([sys.stdin],[],[],0)
      while client_state == ClientState.SEND_ALIVE and len(request[0]) == 0:
         request = select.select([sys.stdin],[],[],0)
      if client_state != ClientState.SEND_ALIVE:
         break   
      command = input().split(' ')
      if command[0].lower() == 'stat':
         print('************ DATOS DEL DISPOSITIVO ************')
         for element in configuration.elements_state:
            print(str(element).ljust(10, ' ') + str(configuration.elements_state[element]))
         print(''.ljust(47,'*'))
      elif command[0].lower() == 'set':
         set_command(command[1:])
      elif command[0].lower() == 'send':
         send_command(command[1:])
      elif command[0].lower() == 'quit':
         client_state = ClientState.DISCONNECTED
         sys.exit(0)
      else:
         print('Error comando(' + command[0] +') no reconocido')  

def set_command(arguments):
   if len(arguments) < 2:
      print('set <identificador_elemento> <nuevo_valor>')
   else:
      if arguments[0] in configuration.elements_state:
         configuration.elements_state[arguments[0]] = arguments[1]
      else:
         print('Error no existe un dispositivo con identificador = ' + str(arguments[0]))

def send_command(arguments):
   global socket_tcp, client_state
   if len(arguments) < 1:
      print('send <identificador_elemento>')
   else:
      if arguments[0] in configuration.elements_state:
         value = configuration.elements_state[arguments[0]]
         info = str(datetime.date.today()).replace(' ',';')
         data = arguments[0].ljust(8,'\0') + str(value).ljust(16,'\0') + info.ljust(80, '\0')
         package = Package(0x20, configuration.id, random_number.decode(), data)
         socket_tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
         socket_tcp.connect((configuration.server, tcp_port))
         socket_tcp.sendall(package.pack())
         inpt,out,ex = select.select([socket_tcp],[],[], 3)
         if len(inpt) != 0:
            package = socket_tcp.recv(1024)
            package = Package.unpack(package, all_data_segment=True)
            if not check_package(package) or package.package_type == PackageDataType.DATA_REJ:
               client_state = ClientState.NOT_REGISTERED      
            elif package.package_type == PackageDataType.DATA_NACK:
               print('Los datos no han sido aceptados por el servidor')
         socket_tcp.close()   
      else:
         print('Error no existe un dispositivo con identificador = ' + str(arguments[0]))

def server_conexions():
   global client_state
   socket_tcpB = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
   socket_tcpB.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
   try:
      socket_tcpB.bind((socket.gethostbyname(socket.gethostname()), configuration.local_tcp))
   except :
      print('\rNo se ha podido abrir conexion')
      client_state = ClientState.DISCONNECTED
   socket_tcpB.listen(1)
   while client_state == ClientState.SEND_ALIVE:
      #wait connection petition
      request = select.select([socket_tcpB], [],[], 0)
      while client_state == ClientState.SEND_ALIVE and len(request[0]) == 0:
         request = select.select([socket_tcpB], [],[], 0)
      if client_state != ClientState.SEND_ALIVE:
         break
      conexion, adress = socket_tcpB.accept()
      request = select.select([conexion], [],[], 0)
      while client_state == ClientState.SEND_ALIVE and len(request[0]) == 0:
         request = select.select([conexion], [],[], 0)
      if client_state != ClientState.SEND_ALIVE:
         break
      package = conexion.recv(1024)      
      package = Package.unpack(package, all_data_segment=True)
      element = package.data[:7].decode()
      package.data = package.data[8:]
      value = package.data[:package.data.find(0)].decode()
      if not check_package(package):
         data = element.ljust(8, '\0') + str(value).ljust(16,'\0') + 'Datos de identificacion incorrectos'.ljust(80,'\0')
         package = Package(PackageDataType.DATA_NACK.value, configuration.id, random_number.decode(), data)
         conexion.sendall(package.pack())
         conexion.close()
         client_state = ClientState.NOT_REGISTERED
      elif package.package_type == PackageDataType.SET_DATA.value:
         if element in configuration.elements_state and element.split('-')[2] == 'I':
            configuration.elements_state[element] = value
            data = element.ljust(8, '\0') + str(value).ljust(16,'\0') + configuration.id.ljust(80,'\0')
            package = Package(PackageDataType.DATA_ACK.value, configuration.id, random_number.decode(), data)
         elif element not in configuration.elements_state:
             data = element.ljust(8, '\0') + str(value).ljust(16,'\0') + 'Identificador de elemento no reconocido'.ljust(80,'\0')
             package = Package(PackageDataType.DATA_NACK.value, configuration.id, random_number.decode(), data)
         else:
            data = element.ljust(8, '\0') + str(value).ljust(16,'\0') + 'El dispositivo no es de entrada'.ljust(80,'\0')
            package = Package(PackageDataType.DATA_NACK.value, configuration.id, random_number.decode(), data)

      elif package.package_type == PackageDataType.GET_DATA.value:
         if element in configuration.elements_state:
            value = configuration.elements_state[element]
            data = element.ljust(8, '\0') + str(value).ljust(16,'\0') + configuration.id.ljust(80,'\0')
            package = Package(PackageDataType.DATA_ACK.value, configuration.id, random_number.decode(), data)
         else:
            data = element.ljust(8, '\0') + str(value).ljust(16,'\0') + 'Identificador de elemento no reconocido'.ljust(80,'\0')
            package = Package(PackageDataType.DATA_NACK.value, configuration.id, random_number.decode(), data)
            
      
      if client_state == ClientState.SEND_ALIVE:
         conexion.sendall(package.pack())    
      conexion.close() 
   socket_tcpB.close()
   time.sleep(1)

def main():
   global configuration, socket_udp, socket_tcp, client_state, tcp_port, command_t
   while client_state != ClientState.DISCONNECTED:
      if client_state == ClientState.NOT_REGISTERED:
         print('Inciando Registro')
         client_state = register_procediment()
      if client_state == ClientState.REGISTERED:
         client_state = ClientState.REGISTERED
         send_alive()
         wait_alive()
         if client_state == ClientState.SEND_ALIVE:
           command_system_thread = threading.Thread(target=command_system)
           send_alive_thread = threading.Thread(target=send_alive)
           wait_alive_thread = threading.Thread(target=wait_alive)
           server_conexions_thread = threading.Thread(target=server_conexions)

           command_system_thread.start()
           send_alive_thread.start()
           wait_alive_thread.start()
           server_conexions_thread.start()
           
           send_alive_thread.join()
           wait_alive_thread.join()
           command_system_thread.join()
           
         
if __name__ == "__main__":  
   setup() 
   main()
    
    
    
