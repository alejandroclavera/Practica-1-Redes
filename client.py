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
   def __init__(self, package_type, id, random_number, data=None, element=None, value=None, info=None):
      self.package_type = package_type
      self.id = id
      self.random_number = random_number
      self.data = data
      self.element = element
      self.value = value
      self.info = info

   def pack(self, pdu_type = 'udp'):
      if pdu_type == 'udp':
         package = struct.pack('!B13s9s61s', self.package_type, self.id.encode(), self.random_number.encode(), self.data.encode())
      elif pdu_type== 'tcp':
            package = struct.pack('!B13s9s8s16s80s', self.package_type, self.id.encode(), self.random_number.encode(), self.element.encode(), self.value.encode(), self.info.encode())
      return package

   @staticmethod
   def unpack(package, pdu_type = 'udp' ,all_data_segment = False):
      if pdu_type == 'udp':
         package_type , package_id, package_random_number, package_data = struct.unpack("!B13s9s61s",package)
         package_id = package_id[:package_id.find(0)].decode()
         package_random_number = package_random_number[:package_random_number.find(0)].decode()
         package_data = package_data[:package_data.find(0)].decode()
         return Package(package_type, package_id, package_random_number, package_data)
      elif pdu_type == 'tcp':
         package_type , package_id, package_random_number, package_element, package_value, package_info = struct.unpack("!B13s9s8s16s80s",package)
         package_id = package_id[:package_id.find(0)].decode()
         package_random_number = package_random_number[:package_random_number.find(0)].decode()
         package_element = package_element[:package_element.find(0)].decode()
         package_value = package_value[:package_value.find(0)].decode()
         package_info = package_info[:package_info.find(0)].decode()
         return Package(package_type, package_id, package_random_number, element=package_element, value=package_value, info=package_info)

   def tostring(self, pdu_type='udp'):
      if pdu_type=='udp':
         return 'tipo = ' + str(self.package_type)  + ',id = ' + self.id + ',rdmn = ' + self.random_number + ',datos = ' + self.data
      elif pdu_type == 'tcp':
         return 'tipo = ' + str(self.package_type)  + ',id = ' + self.id + ',rdmn = ' + self.random_number + ',elemento = ' + self.element + ',valor = ' + self.value + ',info = ' + self.value

def debug_message(msg):
   global debug_mode
   if debug_mode:
      print('\r' + time.strftime('%H:%M:%S')+ ': DEBUG => ' + msg)

def message(msg):
   print('\r' + time.strftime('%H:%M:%S')+ ': MSG => ' + msg)

def setup():
   global configuration, socket_udp, socket_tcp, client_state, debug_mode
   if '-c' in sys.argv:
      arg_pos = sys.argv.index('-c')
      if len(sys.argv[arg_pos:]) < 2:
         print('Error -c <file>')
         sys.exit(0)
      else:
         file = sys.argv[arg_pos + 1]
         configuration = load_configuration(file)
   else:
      configuration = load_configuration(DEFAULT_CONFIGURATION)

   debug_mode = True if '-d' in sys.argv else False
   socket_udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
   socket_tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
   client_state = ClientState.NOT_REGISTERED

def main():
   global configuration, socket_udp, socket_tcp, client_state, tcp_port
   client_state = ClientState.NOT_REGISTERED
   while client_state != ClientState.DISCONNECTED:
      if client_state == ClientState.NOT_REGISTERED:
         client_state = register_procediment()

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

def load_configuration(configuration_file):
   try:
      config_file = open(configuration_file, 'r')
      configuration = {}
      for parametre in config_file:
         parametre = parametre.replace(' ','').split('=')
         parametre_nom = parametre[0]
         parametre_value = parametre[1].replace('\n','')
         configuration[parametre_nom] = parametre_value
      return Configuration(configuration)
   except:
      print("Error no se ha podido cargar la configuración.")
      exit(0)

def register_procediment():
   global register_state, client_state
   register_state = ClientState.NOT_REGISTERED
   n_process = 0
   while n_process < 3:
      message('El cliente pasa al estado: ' + register_state.name + ', proceso de subscripcion ' + str(n_process + 1))
      process_state = register_process()

      if process_state == ClientState.NOT_REGISTERED:#registro no se ha realizado correctamente -> nuevo proceso de registo
         n_process += 1
         time.sleep(2)
      elif process_state == PackageType.REG_REJ:#cliente recibio REG_REJ -> nuevo proceso de registo
         debug_message('El rechazo la petición de registro, el cliente va realizar un nuevo proceso de registro.')
         register_state = ClientState.NOT_REGISTERED
         n_process += 1
         time.sleep(2)
      elif process_state == -1:  #-1 -> iniciar nuevo proceso de registro
         register_state = ClientState.NOT_REGISTERED
         debug_message('El cliente inicia un nuevo proceso de registro')
         n_process += 1
         time.sleep(2)
      else:
         client_state = ClientState.REGISTERED #Cliente pasa a estado REGISTERED
         #Envio y recepcion del primer ALIVE
         send_alive()
         wait_alive()
         # si cliente esta en estado SEND_ALIVE fase de registro superada
         if client_state  == ClientState.SEND_ALIVE:
            debug_message('Fase de registro finalizada')
            return ClientState.SEND_ALIVE

         client_state = ClientState.NOT_REGISTERED #no se recibio paquete ALIVE del servidor -> nuevo proceso de registo
         n_process +=1
   message('Superado el número de procesos de subscripción(3)')
   return ClientState.DISCONNECTED


def register_process():
   global server_id, random_number, register_state, t_wait, tcp_port
   end_process = False
   while not end_process:
      #Fase Envio paquete [REQ_REG]
      register_state = ClientState.WAIT_ACK_REG
      message('El cliente pasa al estado: ' + register_state.name)
      if send_req_packages() == -1: #Envio de paquetes REG_REG
         # no se recibe respueta el cliente pasa a NOT_REGISTERED
         register_state = ClientState.NOT_REGISTERED
         debug_message('No se ha recibido ningun paquete del servidor, el cliente pasa a NOT_REGISTERED.')
         return ClientState.NOT_REGISTERED
      package, server_info = socket_udp.recvfrom(84)
      package = Package.unpack(package, pdu_type='udp')
      debug_message('Recibio: ' + package.tostring())
      #server rechaza el paquete -> nuevo proceso de registro
      if package.package_type == PackageType.REG_REJ.value:
         register_state = ClientState.NOT_REGISTERED
         message('El cliente pasa al estado: ' + register_state.name)
         #notificamos que el servidor ha rechazado el registro
         return PackageType.REG_REJ #notificamos al la función register_procediment
      elif package.package_type == PackageType.REG_NACK.value:
         debug_message('El servidor no acepto el paquete REG_REQ, el cliente vuelve a mandar REQ_REG')
         register_state = ClientState.NOT_REGISTERED
         message('El cliente pasa al estado: ' + register_state.name)
         continue #se recibe REG_NACK se vuelve a mandar REQ_REG sin iniciar proceso de registro
      elif package.package_type == PackageType.REG_ACK.value:
         server_id = package.id
         random_number = package.random_number
         udp_port = int(package.data)
      else:
         register_state = ClientState.NOT_REGISTERED
         debug_message('Se ha recibido un paquete no experado, el cliente pasa al estado NOT_REGISTERED.')
         message('El cliente pasa al estado: ' + register_state.name)
         return -1 #notificamos al la función register_procediment
      #Fase de envio [REG_INFO]
      package = Package(PackageType.REG_INFO.value,configuration.id, random_number, str(configuration.local_tcp) + ','+ configuration.elements)
      register_state = ClientState.WAIT_INFO
      message('El cliente pasa al estado: ' + register_state.name)
      #manda paquete REG_INFO 
      if send_wait_package(package, t_wait * 2, ClientState.WAIT_ACK_INFO, port_udp= udp_port)== -1:
         debug_message('No se ha recibido la contestacion al paquete REG_INFO, el cliente pasa a estado NOT_REGISTERED.')
         register_state = ClientState.NOT_REGISTERED
         message('El cliente pasa al estado: ' + register_state.name)
         return -1 # no se ha recibido respuesta -> Iniciar nuevo proceso de registro
      else:
        package, server_info = socket_udp.recvfrom(84)
        package = Package.unpack(package)
      if check_package(package) and package.package_type == PackageType.INFO_NACK.value:
         register_state = ClientState.NOT_REGISTERED
         message('El cliente pasa al estado: ' + register_state.name)
         continue # recibe  INFO_ACK vuelve a mandar paquetes REG_REQ sin iniciar proceso de registro
      elif check_package(package) and package.package_type == PackageType.INFO_ACK.value:
         tcp_port = int(package.data)
      else:
         register_state = ClientState.NOT_REGISTERED
         debug_message('EL paquete contiene datos de identificacion erroneos')
         message('El cliente pasa al estado: ' + register_state.name)
         return -1 #recibe paquete no experado, se notifica a register_procediment
      end_process = True
   register_state = ClientState.REGISTERED
   message('El cliente pasa al estado: ' + register_state.name)
   return ClientState.REGISTERED

def check_package(package):
   return package.id == server_id and package.random_number == random_number

def send_req_packages():
   global t_wait, register_state
   packetsSend = 0
   t_wait = 1
   t = t_wait
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
               t += t_wait if multiplicator_t < q else 0
            packetsSend += 1
         else :
            return 0
   return -1

def send_wait_package(package, time_wait, next_state, port_udp = None):
   global register_state
   port_udp = port_udp if port_udp != None else configuration.server_udp
   socket_udp.sendto(package.pack(), (configuration.server, port_udp))
   debug_message('Envio: ' +  package.tostring())
   register_state = next_state
   request = select.select([socket_udp], [], [], time_wait)
   return 0 if len(request[0]) != 0 else -1

def send_alive():
   global socket_udp, client_state
   #manda el primer paquete ALIVE
   package = Package(PackageType.ALIVE.value, configuration.id, random_number, '')
   if client_state == ClientState.REGISTERED:
      debug_message('Enviando el primer paquete alive.')
      socket_udp.sendto(package.pack(), (configuration.server, configuration.server_udp))
      debug_message('Envio: ' +  package.tostring())
   else:
      #manda paquetes ALIVE cada 2 segundos mientras el cliente este en SEND_ALIVE
      while client_state == ClientState.SEND_ALIVE:
         socket_udp.sendto(package.pack(), (configuration.server, configuration.server_udp))
         debug_message('Envio: ' +  package.tostring())
         time.sleep(2)

def wait_alive():
   global client_state, socket_udp
   packets_lost = 0
   #Espera el primer paquete ALIVE
   if client_state == ClientState.REGISTERED:
      debug_message('El cliente espera el primer ALIVE del servidor.')
      request = select.select([socket_udp], [], [], 4)
      if len(request[0]) == 0:
         #no recibe respuesta -> cliente pasa a NOT_REGISTERED
         debug_message('El cliente no ha reciviod el primer ALIVE del servidor.')
         client_state = ClientState.NOT_REGISTERED
         message('El cliente pasa al estado: ' + client_state.name)
      else:
         package, server_info= socket_udp.recvfrom(84)
         package = Package.unpack(package)
         debug_message('Recibio: ' + package.tostring())
         #comprueba si el paquete es correcto o si es ALIVE_REJ
         if not check_package(package):
            client_state = ClientState.NOT_REGISTERED
            message('El cliente pasa al estado: ' + client_state.name)
         elif package.package_type == PackageType.ALIVE.value:
            client_state = ClientState.SEND_ALIVE
            message('El cliente pasa al estado: ' + client_state.name)
   else:
      while client_state == ClientState.SEND_ALIVE:
         request = select.select([socket_udp], [], [], 2)
         if len(request[0]) == 0:
            packets_lost += 1# si no recibe el paquete se incrementa el contador de paquetes perdidos
         else:
            packets_lost = 0 #se resetea el contador
            package, server_info= socket_udp.recvfrom(84)
            package = Package.unpack(package)
            debug_message('Recibio: ' + package.tostring())
            id_data = package.data
            #si el paquete no es correcto o se recibe paquete diferenta a ALIVE pasa al estado NOT_REGISTERED
            if package.package_type != PackageType.ALIVE.value or not check_package(package) or id_data != configuration.id:
               debug_message('Paquete erroneo.')
               client_state = ClientState.NOT_REGISTERED
               message('El cliente pasa al estado: ' + client_state.name)
         if packets_lost >= 3:
            #si se pasa el margen de paquetes perdidos el cliente pasa a NOT_REGISTERED
            debug_message('No se han recibido tres paquetes ALIVE seguidos del servidor.')
            client_state = ClientState.NOT_REGISTERED
            message('El cliente pasa al estado: ' + client_state.name)

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
         print('Error comando('+ command[0] +') no reconocido')

def set_command(arguments):
   if len(arguments) < 2:
      print('set <identificador_elemento> <nuevo_valor>')
   else:
      if arguments[0] in configuration.elements_state:
         configuration.elements_state[arguments[0]] = arguments[1]
      else:
         print('Error no existe un dispositivo con identificador = ' + str(arguments[0]))

def send_command(arguments):
   global client_state
   if len(arguments) < 1:
      print('send <identificador_elemento>')
   else:
      if arguments[0] in configuration.elements_state:
         value = configuration.elements_state[arguments[0]]
         info = str(datetime.date.today()).replace(' ',';')
         package = Package(0x20, configuration.id, random_number, element=arguments[0],value=str(value), info=info)
         debug_message('Fase de envio de datos al servidor')
         socket_tcpB = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
         socket_tcpB.connect((configuration.server, tcp_port))
         socket_tcpB.sendall(package.pack(pdu_type= 'tcp'))
         debug_message('Envio: ' +  package.tostring(pdu_type='tcp'))
         request = select.select([socket_tcpB],[],[], 3)
         if len(request) != 0:
            package = socket_tcpB.recv(1024)
            package = Package.unpack(package, pdu_type='tcp')
            debug_message('Recibio: ' + package.tostring(pdu_type='tcp'))
            if not check_package(package) or package.package_type == PackageDataType.DATA_REJ:
               debug_message('El servidor a rechazado los datos')
               client_state = ClientState.NOT_REGISTERED
            elif package.package_type == PackageDataType.DATA_NACK:
               debug_message('Los datos no han sido aceptados por el servidor')
         else:
            debug_message('No se ha recibido respuesta del servidor')
         socket_tcpB.close()
      else:
         print('Error no existe un dispositivo con identificador = ' + str(arguments[0]))

def server_conexions():
   global socket_tcp ,client_state
   socket_tcp = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
   socket_tcp.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
   try:
      #abre canal TCP
      socket_tcp.bind(('127.0.0.1', configuration.local_tcp))
      debug_message('El client abre el puerto' + str(configuration.local_tcp) + "para la comunicacion TCP")
   except :
      message("No se ha podido abrir el puerto TCP")
      client_state = ClientState.DISCONNECTED
   socket_tcp.listen(1)
   #mantiene el puerto abierto mientras el cliente este en SEND_ALIVE
   while client_state == ClientState.SEND_ALIVE:
      #espera conexiones
      request = select.select([socket_tcp], [],[], 0)
      while client_state == ClientState.SEND_ALIVE and len(request[0]) == 0:
         request = select.select([socket_tcp], [],[], 0)
      if client_state != ClientState.SEND_ALIVE:
         break
      conexion, address = socket_tcp.accept()
      request = select.select([conexion], [],[], 0)
      while client_state == ClientState.SEND_ALIVE and len(request[0]) == 0:
         request = select.select([conexion], [],[], 0)
      if client_state != ClientState.SEND_ALIVE:
         break
      package = conexion.recv(1024)
      package = Package.unpack(package, pdu_type='tcp')
      debug_message('Recibio: ' + package.tostring(pdu_type='tcp'))
      element = package.element
      value = package.value
      #Manda paquete en función del estado del paquete
      if not check_package(package):
         #paquete erroneo cierra conexion
         debug_message('Envio: ' + package.tostring(pdu_type='tcp'))
         package = Package(PackageDataType.DATA_NACK.value, configuration.id, random_number, element=element, value=str(value), info='Datos de identificacion incorrectos')
         conexion.sendall(package.pack(pdu_type= 'tcp'))
         conexion.close()
         client_state = ClientState.NOT_REGISTERED
      elif package.package_type == PackageDataType.SET_DATA.value:
         if element in configuration.elements_state and element.split('-')[2] == 'I':
            configuration.elements_state[element] = value
            package = Package(PackageDataType.DATA_ACK.value, configuration.id, random_number, element=element, value=str(value), info=configuration.id)
         elif element not in configuration.elements_state:
             package = Package(PackageDataType.DATA_NACK.value, configuration.id, random_number, element=element, value=str(value), info='Identificador de elemento no reconocido')
         else:
            package = Package(PackageDataType.DATA_NACK.value, configuration.id, random_number, element=element, value=str(value), info='El dispositivo no es de entrada')
      elif package.package_type == PackageDataType.GET_DATA.value:
         if element in configuration.elements_state:
            value = configuration.elements_state[element]
            package = Package(PackageDataType.DATA_ACK.value, configuration.id, random_number, element=element, value=str(value), info=configuration.id)
         else:
            package = Package(PackageDataType.DATA_NACK.value, configuration.id, random_number, element=element, value=str(value), info='Identificador de elemento no reconocido')
      if client_state == ClientState.SEND_ALIVE:
         conexion.sendall(package.pack(pdu_type='tcp'))
      conexion.close()
   socket_tcp.close()


if __name__ == '__main__':
   setup()
   main()
