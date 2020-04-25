#define _GNU_SOURCE
#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<errno.h>
#include<time.h>
#include<ctype.h>
#include<sys/mman.h>
#include<string.h>
#include<signal.h>
#include<sys/types.h>
#include<sys/wait.h>
#include<sys/select.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>

#define DEFAULT_CONFIGURATION "server.cfg"
#define CLIENT_BBDD "bbdd_dev.dat"

enum CLIENT_STATS
{
   DISCONNECTED = 0xa0,
   NOT_REGISTERED = 0xa1,
   WAIT_ACK_REG = 0xa2,
   WAIT_INFO = 0xa3,
   WAIT_ACK_INFO = 0xa4,
   REGISTERED = 0xa5,
   SEND_ALIVE = 0xa6
};

enum PACKAGE_TYPES
{
   REG_REQ = 0X00,
   REG_INFO = 0X01,
   REG_ACK = 0X02,
   INFO_ACK = 0X03,
   REG_NACK = 0X04,
   INFO_NACK = 0X05,
   REG_REJ = 0X06,
   ALIVE = 0x10,
   ALIVE_REJ = 0x11,
};

enum PACKAGEDATATYPE
{
   SEND_DATA = 0x20,
   SET_DATA = 0x21,
   GET_DATA = 0x22,
   DATA_ACK = 0x23,
   DATA_NACK = 0x24,
   DATA_REJ = 0x25,
};

typedef struct
{
   char id[13];
   int udp_port;
   int tcp_port;
}server_configuration;

typedef struct
{
   char id[13];
   char random_number[9];
   int stat;
   char devices[100];
   struct sockaddr_in ip;
   int tcp_port;
   int package_lost;
}client;

typedef struct
{
   unsigned char package_type;
   char id[13];
   char random_number[9];
   char data[61];

}udp_pdu;

typedef struct
{
   unsigned char package_type;
   char id[13];
   char random_number[9];
   char element[8];
   char value[16];
   char info[80];

}tcp_pdu;

//*********************
//    Funciones
//*********************
void load_args(int argc, char *argv[] ,char *config_path, char *ddbb_path);
int load_configuration(char* path, server_configuration* configuration);
client* read_bbdd(int* num_clients);
client* find_client(char* id);
void package(udp_pdu *package, unsigned char type, char *id, char *rdn, char *data);
void check_comunication();
//UDP
void udp_signalhandler(int signal);
void udp_control();
int open_udp_channel(int *socket_udp, int port);
void register_process(udp_pdu *package, struct sockaddr_in* addr_client, int *laddr_client);
void clients_comunication(udp_pdu *client_package, struct sockaddr_in* addr_client, int *laddr_client);
int wait_package(int socket, int time);
void send_package(int socket, udp_pdu *package, struct sockaddr *addr_client, int *laddr_client);
//TCP
int open_tcp_channel(int *socket_tcp, int port);
void tcp_control();
void attend_conexion(int socket, struct sockaddr_in *addr_client, int *laddr_client);
void info_protocol(tcp_pdu *info_package, client *client);
//consola del sistema
void commands_system();
void set_command();
void get_command();
void quit();
void msg_system(char *msg);
void debug_message(char *msg);
void error_system(char *msg);
//Funciones auxiliares
void disconnected(client *client);
void strlwr(char *str);
int write_info_client(char *client_info, client *client);
void get_time(char *time2format, int size, int all);
char *get_stat_name(int stat);
//************************
//    Variables Locales
//************************
server_configuration configuration;
client *clients;
int num_clients;
int socket_udp;
int socket_tcp;
int controler_pid;
int udp_pid;
int tcp_pid;
int checkconexion_pid;
int debug_mode;

//*********************
//    Signal Handlers
//*********************
void controler_signalhandler(int signal)
{
   if(signal == SIGINT)
   {
      msg_system("Proceso cerrado por ^C");
      kill(0, SIGINT);
      exit(0);
   }
}

void udp_signalhandler(int signal)
{
   if(signal == SIGINT)
   {
      close(socket_udp);
      exit(0);
   }
}

void tcp_signalhandler(int signal)
{
   if(signal == SIGINT)
   {
      close(socket_tcp);
      exit(0);
   }
}

int main(int argc, char *argv[])
{
   char config_path[100];
   char ddbb_path[100];
   //Carga de Argumentos
   load_args(argc ,argv,config_path, ddbb_path);

   //cargamos la configuracion del servidor
   if(load_configuration(config_path, &configuration) < 0)
   {
      error_system("No se a podido cargar el archivo de configuración");
      exit(-1);
   }

   //leemos de la base de datos los clientes permitidos
   if((clients = read_bbdd(&num_clients)) == NULL)
   {
      error_system("No se a podido leer la base de datos de clientes");
      exit(-1);
   }
   //Inciamos el proceso del protocolo UDP
   udp_pid = fork();
   if(udp_pid<0)
   {
      error_system("No se ha podido crear el proceso que atienda los paquetes UDP");
   }
   else if(udp_pid == 0){
      signal(SIGINT, udp_signalhandler);
      udp_control();
   }
   //Iniciamos el proceso que controle las comunicaciones del servidor con los clientes
   if(fork())
   {
      signal(SIGINT, udp_signalhandler);
      check_comunication();
   }
   //Inciamos el proceso del protocolo TCP
   tcp_pid = fork();
   if(tcp_pid<0)
   {
      fprintf(stderr, "Error no se ha podido crear proceso.");
   }
   else if(tcp_pid == 0)
   {
      signal(SIGINT, tcp_signalhandler);
      tcp_control();
   }

   signal(SIGINT, controler_signalhandler);
   commands_system();
}


void load_args(int argc, char *argv[] ,char *config_path, char *ddbb_path)
{
   debug_mode = 0;
   strcpy(config_path, "");
   strcpy(ddbb_path, "");
   if(argc > 1)
   {
      for(int i = 1; i < argc; i++)
      {
         if(strcmp(argv[i], "-d") == 0)
         {
            debug_mode = 1;
         }
         else if(strcmp(argv[i], "-c")== 0)
         {
            if(i + 1 < argc)
               strcpy(config_path, argv[i + 1]);

         }
         else if(strcmp(argv[i], "-u")== 0)
         {
            if(i + 1 < argc)
               strcpy(ddbb_path, argv[i + 1]);
         }
      }
   }

   if(strlen(config_path)==0)strcpy(config_path, DEFAULT_CONFIGURATION);
   if(strlen(ddbb_path)==0)strcpy(ddbb_path, CLIENT_BBDD);
}

int load_configuration(char* path, server_configuration* configuration)
{
   FILE *file = fopen(path, "r");
   char buffer[1024];
   char *parametre_nom , *parametre_value;
   int num_parametres = 0;
   if(file == NULL)
      return -1;

   while(fgets(buffer, 1024, file)!=NULL)
   {
      parametre_nom = strtok(buffer, " ");
      strtok(NULL, " ");
      parametre_value = strtok(NULL, " ");

      if(parametre_nom != NULL && parametre_value != NULL)
      {
         if(strcmp(parametre_nom, "Id")== 0)
         {
            parametre_value = strtok(parametre_value, "\n");
            strcpy(configuration->id, parametre_value);
            num_parametres += 1;
         }
         else if(strcmp(parametre_nom, "UDP-port")== 0)
         {
            parametre_value = strtok(parametre_value, "\n");
            configuration->udp_port = atoi(parametre_value);
            num_parametres += 1;
         }
         else if(strcmp(parametre_nom, "TCP-port")== 0)
         {
            parametre_value = strtok(parametre_value, "\n");
            configuration->tcp_port = atoi(parametre_value);
            num_parametres += 1;
         }
      }
   }
   if(num_parametres != 3)
   {
      error_system("Formato del archivo de configuracion erroneo.");
      return -1;
   }
   return 0;
}

client* read_bbdd(int* num_clients)
{
   client *client_list;
   FILE *file = fopen(CLIENT_BBDD, "r");
   char buffer[1024];
   char *id;
   if(file == NULL) return NULL;
   //compartimos la información de los clientes a todos los procesos
   client_list = mmap(NULL, 20 * sizeof(client), PROT_READ | PROT_WRITE, MAP_SHARED |  MAP_ANONYMOUS, -1, 0);
   *num_clients = 0;
   while(fgets(buffer, 1024, file))
   {
     id = strtok(buffer, "\n");
     if(id != NULL)
     {
         memset(&client_list[*num_clients], 0, sizeof(client));
         strcpy(client_list[*num_clients].random_number, "-");
         client_list[*num_clients].stat = DISCONNECTED;
         strcpy(client_list[(*num_clients)++].id, id);
     }
   }
    return client_list;
}

client* find_client(char* id)
{
   int i;
   for(i = 0; i < num_clients; i++)
   {
      if(strcmp(id, clients[i].id) == 0)
         return &clients[i];
   }
   return NULL;
}

void package(udp_pdu *package, unsigned char type, char *id, char *rdn, char *data)
{
  memset(package, 0, sizeof(udp_pdu));
  package->package_type = type;
  strcpy(package->id, id);
  strcpy(package->random_number, rdn);
  strcpy(package->data, data);
}


//UDP Protocol
int open_udp_channel(int *socket_udp, int port)
{
   struct sockaddr_in addr_server;
   if((*socket_udp = socket(AF_INET,SOCK_DGRAM,0)) < 0)
		return -1;
   memset(&addr_server, 0, sizeof(struct sockaddr_in));
   addr_server.sin_family = AF_INET;
   addr_server.sin_addr.s_addr = INADDR_ANY;
   addr_server.sin_port= htons(port);
   if(bind(*socket_udp,(struct sockaddr* )&addr_server,(socklen_t)sizeof(struct sockaddr_in)) < 0)
      return -1;
   return 0;
}

int wait_package(int socket, int time)
{
   fd_set readfd;
   struct timeval timeout;

   FD_ZERO(&readfd);
   FD_SET(socket, &readfd);

   timeout.tv_sec = time;
   timeout.tv_usec = 0;
   return select(socket + 1, &readfd, NULL, NULL,&timeout);
}


void send_package(int socket, udp_pdu *package, struct sockaddr *addr_client, int *laddr_client)
{
   int nbytes =  sendto(socket, package, sizeof(udp_pdu), 0, addr_client, *laddr_client);
   if (nbytes < 0)
   {
      perror("Error ");
      exit(0);
   }
}

void udp_control()
{
   struct sockaddr_in addr_client;
   int laddr_client;
   udp_pdu client_package;
   int size;
   int pid;
   // Abrimos el canal UDP
   if(open_udp_channel(&socket_udp, configuration.udp_port) == -1)
   {
      fprintf(stderr ,"Error no se ha podido abrir la conexion udp");
      close(socket_udp);
      exit(-1);
   }
   debug_message("Puerto UDP abierto");
   while (1)
   {
      //el servidor se mantiene a la espera de paquetes UDP
      laddr_client = sizeof(addr_client);
      size = recvfrom(socket_udp, &client_package, sizeof(udp_pdu), 0, (struct sockaddr *)&addr_client, (socklen_t *)&laddr_client);
      if (size == -1)
      {
         perror("Error UDP");
      }

      debug_message("Se recibio paquete UDP, creando proceso para antenderlo");
      if((pid = fork())< 0)
      {
            debug_message("No se pudo crear el proceso");
      }
      else if(pid == 0)
      {
         //Recibe petición de registro
         if(client_package.package_type == REG_REQ)
         {
            debug_message("Se recibio paquete REG_REQ");
            srand(time(NULL));
            register_process(&client_package, &addr_client, &laddr_client);
         }//recibe paquete de mantenimiento de comunicación
         else if(client_package.package_type == ALIVE)
         {
            debug_message("Se recibio paquete ALIVE");
            clients_comunication(&client_package, &addr_client, &laddr_client);
         }
         exit(0);
      }
   }
   close(socket_udp);
}

void register_process(udp_pdu *client_package, struct sockaddr_in* addr_client, int *laddr_client)
{
   client *client_to_register;
   struct sockaddr_in addr_server;
   socklen_t laddr_server = sizeof(addr_server);
   udp_pdu package_to_send;
   int socket_udp_register;
   char new_port[100];
   char tcp_port[100];
   char *client_tcp_port;
   char *elements;
   int rdn;
   char msg[100];
   memset(&package_to_send, 0, sizeof(udp_pdu));
   //Recibe paquete de cliente no permitido -> Envio REG_REJ
   if((client_to_register = find_client(client_package->id)) == NULL)
   {
      debug_message("Se recibio un paquete con la id incorrecta, se rechaza la petición.");
      package(&package_to_send, REG_REJ, configuration.id, "00000000", "Id incorrecta");
      send_package(socket_udp, &package_to_send, (struct sockaddr *)addr_client, laddr_client);
      exit(0);
   } // EL cliente ya mandado anteriormente el paquete REG_REQ -> Envio REG_REJ
   else if(client_to_register->stat != DISCONNECTED)
   {
      debug_message("Se recibio un paquete REG_REQ de un usuario ya registrado, se rechaza la petición.");
      package(&package_to_send, REG_REJ, configuration.id, "00000000", "Usuario ya registrado");
      send_package(socket_udp, &package_to_send, (struct sockaddr *)addr_client, laddr_client);
      exit(0);
   }// EL el rdnm no es todo ceros -> Envio REG_REJ
   else if(strcmp(client_package->random_number, "00000000") !=0)
   {
      debug_message("Se recibio un paquete REG_REQ con el numero aleatorio erroneo, se rechaza la petición.");
      package(&package_to_send, REG_REJ, configuration.id, "00000000", "número aleatorio incorrecto");
      send_package(socket_udp, &package_to_send, (struct sockaddr *)addr_client, laddr_client);
      exit(0);
   }// EL campo datos no esta vacio -> Envio REG_REJ
   else if(strlen(client_package->data) !=0)
   {
      debug_message("Se recibio un paquete REG_REQ con campo de datos incorrecto ");
      package(&package_to_send, REG_REJ, configuration.id, "00000000", "campo de datos incorrecto.");
      send_package(socket_udp, &package_to_send, (struct sockaddr *)addr_client, laddr_client);
      exit(0);
   }
   //Cargamos valores para empaquetar
   rdn = rand() % 99999999 + 1;
   open_udp_channel(&socket_udp_register, 0); //abre nuevo puerto UDP, puerto por donde se realizara el resto de la fase de registro
   getsockname(socket_udp_register, (struct sockaddr *)&addr_server, &laddr_server);
   client_to_register->ip = (*addr_client);
   sprintf(client_to_register->random_number, "%d", rdn);
   sprintf(new_port, "%d", ntohs(addr_server.sin_port));
   package(&package_to_send, REG_ACK, configuration.id, client_to_register->random_number, new_port);
   debug_message("Envio de paquete REG_ACK");
   send_package(socket_udp, &package_to_send, (struct sockaddr *)addr_client, laddr_client);
   client_to_register->stat = WAIT_ACK_INFO;
   sprintf(msg,"Dispostivo: %s pasa a estado WAIT_INFO", client_to_register->id);
   msg_system(msg);
   close(socket_udp);
   //Espera del REG_INFO
   if(wait_package(socket_udp_register, 2)== 0)
   {
      debug_message("No se ha recibido el paquete REG_INFO.");
      disconnected(client_to_register);
      exit(0);
   }
   recvfrom(socket_udp_register, client_package, sizeof(udp_pdu), 0, (struct sockaddr *)addr_client, (socklen_t *)laddr_client);
   //recibe paquete no esperado  -> El cliente pasa a estar DISCONNECTED
   if(client_package->package_type != REG_INFO)
   {
      debug_message("Se ha recibido un paquete diferente al esperado");
      disconnected(client_to_register);
   }// La id del cliente no es valida -> Envia INFO_NACK
   if(strcmp(client_package->id, client_to_register->id) !=0)
   {
      debug_message("Se recibio un paquete REG_INFO con id incorrecta");
      package(&package_to_send, INFO_NACK, configuration.id, client_to_register->random_number, "Id cliente incorrecta.");
      send_package(socket_udp_register, &package_to_send, (struct sockaddr *)addr_client, laddr_client);
      exit(0);
   }// La id del cliente no es valida -> Envia INFO_NACk
   else if(strcmp(client_package->random_number, client_to_register->random_number)!=0)
   {
      debug_message("Se recibio un paquete REG_INFO con el numero aleatorio erroneo");
      package(&package_to_send, INFO_NACK, configuration.id, client_to_register->random_number, "Número aleatorio incorrecto.");
      send_package(socket_udp_register, &package_to_send, (struct sockaddr *)addr_client, laddr_client);
      exit(0);
   }
   // Guarada la informacion del campo data del paqute en la informacion del cliente
   client_tcp_port = strtok(client_package->data, ",");
   elements = strtok(NULL, ",");
   if(client_tcp_port == NULL || elements == NULL)
   {
      debug_message("Elcontenido del paquete REG_INFO recibido es erroneo, se envia el paquete INFO_ACK");
      package(&package_to_send, INFO_NACK, configuration.id, client_to_register->random_number, "Campo datos erroneo.");
      send_package(socket_udp_register, &package_to_send, (struct sockaddr *)addr_client, laddr_client);
      exit(0);
   }
   sprintf(tcp_port, "%d", configuration.tcp_port);
   package(&package_to_send, INFO_ACK, configuration.id, client_to_register->random_number, tcp_port);
   send_package(socket_udp_register, &package_to_send, (struct sockaddr *)addr_client, laddr_client);
   client_to_register->stat = REGISTERED;
   sprintf(msg,"Dispostivo: %s pasa a estado REGISTERED", client_to_register->id);
   msg_system(msg);
   client_to_register->tcp_port = atoi(client_tcp_port);
   strcpy(client_to_register->devices, elements);
   //Espera del primer paquete ALIVE
   sleep(3);
   if(client_to_register->stat != SEND_ALIVE) // Si el cliente no esta en SEND_ALIVE -> no se ha recibido el primer ALIVE
   {
      debug_message("No se ha recibido el primer ALIVE.");
      disconnected(client_to_register);
   }
   exit(0);
}


void clients_comunication(udp_pdu *client_package, struct sockaddr_in* addr_client, int *laddr_client)
{
   udp_pdu package_to_send;
   client *client;
   char msg[100];
   memset(&package_to_send, 0, sizeof(udp_pdu));
   //Recibe paquete de cliente no permitido -> Envio ALIVE_REJ
   if((client = find_client(client_package->id)) == NULL)
   {
      debug_message("Se ha recibido un paquete ALIVE con id incorrecta.");
      package(&package_to_send,ALIVE_REJ , configuration.id, "00000000", "Id incorrecta");
      send_package(socket_udp, &package_to_send, (struct sockaddr *)addr_client, laddr_client);
      exit(0);
   }//Recibe paquete de cliente con rdnm incorrecto-> Envio ALIVE_REJ
   else if(strcmp(client_package->random_number, client->random_number) !=0)
   {
      debug_message("Se ha recibido un paquete ALIVE con el número aleatorio erroneo.");
      package(&package_to_send, ALIVE_REJ, configuration.id, client->random_number, "número aleatorio incorrecto");
      send_package(socket_udp, &package_to_send, (struct sockaddr *)addr_client, laddr_client);
      exit(0);
   }//Recibe un paqute de un cliente que no esta en el estado REGISTERED o SEND_ALIVE -> Envia ALIVE_REJ
   else if(client->stat != REGISTERED && client->stat != SEND_ALIVE)
   {
      debug_message("Se ha recibido un paquete ALIVE de un dispostivo no registrado.");
      package(&package_to_send, ALIVE_REJ, configuration.id, client->random_number, "El dispostivo no esta registrado");
      send_package(socket_udp, &package_to_send, (struct sockaddr *)addr_client, laddr_client);
      exit(0);
   }

   //Si todo esta correcto envia un paqute ALIVE al cliente
   package(&package_to_send, ALIVE, configuration.id, client->random_number, client->id);
   send_package(socket_udp, &package_to_send, (struct sockaddr *)addr_client, laddr_client);
   //Si el cliente estaba REGISTERED pasa a SEND_ALIVE
   if(client->stat == REGISTERED)
   {
      client->stat = SEND_ALIVE;
      sprintf(msg,"Dispostivo: %s pasa a estado SEND_ALIVE", client->id);
      msg_system(msg);
   }
   // reinicia el contador de paquetes perdidos(importante para determinar si se ha perdido la conexion con el cliente)
   client->package_lost = 0;
   exit(0);
}

void check_comunication()
{
   char msg[100];
   while (1){
   for(int i = 0; i <num_clients; i++)
   {
      //Incrementa en uno el contador de paquetes perdidos
      if(clients[i].stat == SEND_ALIVE)
      {
         if(clients[i].package_lost < 3)
         {
            clients[i].package_lost += 1;
         }
         //Si el número de paquetes perdidos supera el margen el el servidor interpreta que se ha perdido la conexion con el cliente
         if(clients[i].package_lost >= 3)
         {
            clients[i].package_lost = 0;
            sprintf(msg, "Se ha dejado de recibir 3 paquetes consecutivos del dispostivo con id %s", clients[i].id);
            debug_message(msg);
            disconnected(&clients[i]);
         }
      }
   }
      sleep(2);
   }
}


//TCP
void tcp_control()
{
   struct sockaddr_in addr_client;
   int laddr_client;
   int client_socket;
   int pid;
   //abre canal TCP
   if(open_tcp_channel(&socket_tcp, configuration.tcp_port) == -1)
   {
      error_system("No se puedo abrir el canal TCP");
      close(socket_tcp);
      exit(-1);
   }
   debug_message("Puerto TCP abierto");
   listen(socket_tcp, 5);

   while (1)
   {
      //se mantiene a la expera de conexiones TCP
      laddr_client = sizeof(struct sockaddr_in);
      client_socket = accept(socket_tcp, (struct sockaddr *)&addr_client, (socklen_t *)&laddr_client);
      debug_message("Se recibio una conexion TCP, creando proceso para antenderlo");
      //recibe una petición de conexion tcp, se crea un proceso para atenderla
      if(client_socket < 0)
      {
         error_system("No se ha podido aceptar la conexion con el servidor");
      }
      else if((pid = fork()) < 0)
      {
         error_system("No se ha podido crear un proceso para atender la conexion");
         close(client_socket);
      }
      else if(pid == 0){
        attend_conexion(client_socket, &addr_client, &laddr_client);
      }
      else
      {
        close(client_socket);
      }
   }
}

int open_tcp_channel(int *socket_tcp, int port)
{
   struct sockaddr_in addr_server;
   if((*socket_tcp = socket(AF_INET,SOCK_STREAM,0)) < 0)
		return -1;
   memset(&addr_server, 0, sizeof(struct sockaddr_in));
   addr_server.sin_family = AF_INET;
   addr_server.sin_addr.s_addr = INADDR_ANY;
   addr_server.sin_port= htons(port);
   if(bind(*socket_tcp,(struct sockaddr* )&addr_server,(socklen_t)sizeof(struct sockaddr_in)) < 0)
      return -1;
   return 0;
}

void attend_conexion(int socket, struct sockaddr_in *addr_client, int *laddr_client)
{
   tcp_pdu client_package;
   tcp_pdu package_to_send;
   client *client;
   char client_info[100];
   //espera a recibir un paquete TCP del cliente
   if(wait_package(socket, 3) == 0)
   {
      //timeout-> no ha recibido ningun paquete del client por lo que cierra la conexion
      debug_message("No se ha recibio ningun paquete cerrando conexion TCP");
      close(socket);
      exit(0);
   }
   recv(socket, &client_package, sizeof(tcp_pdu), 0);
   //recibe paquete con cliente no permitido -> DATA_REJ
   if((client = find_client(client_package.id)) == NULL)
   {
      package((udp_pdu *)&package_to_send, DATA_REJ, client->id, client->random_number, "");
      strcpy(package_to_send.element, client_package.element);
      strcpy(package_to_send.value, client_package.value);
      strcpy(package_to_send.info, client_package.info);
      send(socket, &package_to_send, sizeof(tcp_pdu), 0);
      disconnected(client);
      close(socket);
      exit(0);
   }// rdmn incorrecto -> DATA_REJ
   else if(strcmp(client_package.random_number, client->random_number) != 0)
   {
      package((udp_pdu *)&package_to_send, DATA_REJ, client->id, client->random_number, "");
      strcpy(package_to_send.element, client_package.element);
      strcpy(package_to_send.value, client_package.value);
      strcpy(package_to_send.info, client_package.info);
      send(socket, &package_to_send, sizeof(tcp_pdu), 0);
      disconnected(client);
      close(socket);
      exit(0);
   }
   //Guarda información en fichero
   sprintf(client_info, "%s;%s;%s", "SEND_DATA", client_package.element, client_package.value);
   if(write_info_client(client_info, client) == -1)
   {
      //no ha podido guardar información fichero -> DATA_NACK
      package((udp_pdu *)&package_to_send, DATA_NACK, configuration.id, client->random_number, "");
      strcpy(package_to_send.info, "No se ha podido guardar la información");
   }
   else
   {
      package((udp_pdu *)&package_to_send, DATA_ACK, configuration.id, client->random_number, "");
      strcpy(package_to_send.info, client_package.id);
   }
   strcpy(package_to_send.element, client_package.element);
   strcpy(package_to_send.value, client_package.value);
   send(socket, &package_to_send, sizeof(tcp_pdu), 0);
   close(socket);
   exit(0);
}

void view_clients()
{
   char *stat_name;
   printf("-----Id.---- --RNDM-- ------ IP ----- ---ESTADO----  ---ELEMENTOS------------------------------------------\n");
   for(int i = 0; i < num_clients; i++)
   {
      stat_name = get_stat_name(clients[i].stat);
      if(clients[i].stat != DISCONNECTED)
         printf("%s %s   %s\t %s  %s\n", clients[i].id, clients[i].random_number, inet_ntoa(clients[i].ip.sin_addr), stat_name ,clients[i].devices);
      else
         printf("%s\t\t\t\t%s\n", clients[i].id, stat_name);
   }
}

void commands_system()
{
   int command_in_buffer;
   char command_read[1024];
   char *command;
   while (1)
   {
      command_in_buffer = 0;
      while(command_in_buffer == 0)
         command_in_buffer = wait_package(0, 0);
      fgets(command_read, 1024, stdin);

      if(command_read[0] != '\n'){
         command_read[strlen(command_read)-1] = '\0';
         command = strtok(command_read, " ");
         strlwr(command);
         if(strstr(command, "list"))
            view_clients();
         else if(strstr(command, "set"))
            set_command();
         else if(strstr(command, "get"))
            get_command();
         else if(strstr(command, "quit"))
            quit();
         else
            printf("Error comando (%s) no reconocido.\n", command_read);
      }

   }
}

void get_command()
{
   char *id, *element;
   char msg[1024];
   client *client;
   tcp_pdu package_to_send;

   id = strtok(NULL, " ");
   element = strtok(NULL, " ");
   //El comando no recibe ningun argumento
   if(id == NULL || element == NULL)
   {
      msg_system("Error de sintaxis (get <id_controlador> <elemento> )");
   }// id del cliente no valida
   else if((client = find_client(id)) == NULL){
      sprintf(msg,"Dispostivo %s no esta en la base de datos", id);
      msg_system(msg);
   }//el cliente no  tiene el elemento pasado por agumento
   else if(strstr(client->devices, element) == 0)
   {
      sprintf(msg,"Dispostivo %s no tiene el elemento %s", id, element);
      msg_system(msg);
   }
   else
   {  //manda el paquete GET_DATA
      package_to_send.package_type = GET_DATA;
      strcpy(package_to_send.id, configuration.id);
      strcpy(package_to_send.random_number, client->random_number);
      strcpy(package_to_send.element, element);
      strcpy(package_to_send.value, "");
      strcpy(package_to_send.info, client->id);
      info_protocol(&package_to_send, client);
   }
}

void set_command()
{
   char *id, *element, *value;
   char msg[1024];
   client *client;
   tcp_pdu package_to_send;

   id = strtok(NULL, " ");
   element = strtok(NULL, " ");
   value = strtok(NULL, " ");
   //El comando no recibe ningun argumento
   if(id == NULL || element == NULL || value == NULL )
   {
      msg_system("Error de sintaxis (set <id_controlador> <elemento> <valor> )");
   }// id del cliente no valida
   else if((client = find_client(id)) == NULL){
      sprintf(msg,"Dispostivo %s no esta en la base de datos", id);
      msg_system(msg);
   }//el cliente no  tiene el elemento pasado por agumento
   else if(strstr(client->devices, element) == 0)
   {
      sprintf(msg,"Dispostivo %s no tiene el elemento %s", id, element);
      msg_system(msg);
   }//el elemento no es de entrada
   else if(strstr(element, "-I") == 0)
   {
      sprintf(msg,"El elemento %s no es un elemento de entrada", element);
      msg_system(msg);

   }
   else {
      //manda paqute SEND_DATA
      package_to_send.package_type = SET_DATA;
      strcpy(package_to_send.id, configuration.id);
      strcpy(package_to_send.random_number, client->random_number);
      strcpy(package_to_send.element, element);
      strcpy(package_to_send.value, value);
      strcpy(package_to_send.info, client->id);
      info_protocol(&package_to_send, client);
   }
}

void quit()
{
   signal(SIGINT, SIG_IGN);
   kill(0, SIGINT);
   wait(NULL);
   wait(NULL);
   exit(0);
}

void info_protocol(tcp_pdu *info_package, client *client)
{
   int client_socket;
   struct sockaddr_in addr_client;
   tcp_pdu client_package;
   char client_info[100];
   client_socket = socket(AF_INET,SOCK_STREAM,0);
   memset(&addr_client, 0, sizeof(struct sockaddr_in));
   addr_client.sin_family = AF_INET;
   addr_client.sin_addr = client->ip.sin_addr;
   addr_client.sin_port= htons(client->tcp_port);
   //conexion TCP con el cliente
   if(connect(client_socket, (struct sockaddr *)&addr_client, sizeof(struct sockaddr_in)) < 0)
   {
      error_system("Error no se ha podido conectar con el cliente via TCP");
   }
   else
   {
      debug_message("Envio paquete al cliente via TCP");
      //manda paquete de datos TCP
      send(client_socket, info_package, sizeof(tcp_pdu), 0);
      //espera la respuesta del cliente
      if(wait_package(client_socket, 3) == 0)
      {
         //no se recibe respuesta -> cierra la conexion
         debug_message("No se recibio contestacion por parte del cliente");
         close(client_socket);
      }else
      {//El cliente no acepta los datos
         recv(client_socket, &client_package, sizeof(tcp_pdu), 0);
         if(client_package.package_type == DATA_NACK)
         {
            debug_message("El cliente no se han aceptado los datos");
         }// El cliente rechaza los datos -> Desconexions del cliente
         else if(client_package.package_type == DATA_REJ)
         {
            debug_message("El cliente ha rechazado los datos");
            disconnected(client);
         }else if(client_package.package_type == DATA_ACK)
         {
            debug_message("El cliente a aceptado los datos");
            if(info_package->package_type== GET_DATA)
               sprintf(client_info, "GET_DATA;%s;%s",client_package.element, client_package.value);
            else
               sprintf(client_info, "SET_DATA;%s;%s",info_package->element, info_package->value);
            debug_message("Escribiendo en la base de datos del cliente");
            write_info_client(client_info, client);// almacena los datos en el fichero del cliente
         }
      }
   }
   close(client_socket);
}

//Funciones auxiliares
void disconnected(client *client)
{
   char msg[100];
   client->stat = DISCONNECTED;
   strcpy(client->random_number, "-");
   strcpy(client->devices, "");
   sprintf(msg, "Dispostivo: %s pasa al estado DISCONNECTED", client->id);
   msg_system(msg);
}

void strlwr(char *str)
{
  for(int i = 0; i < strlen(str); i++)
  {
     str[i] = tolower(str[i]);
  }
}

int write_info_client(char *client_info, client *client)
{
   FILE *client_file;
   char file_path[20];
   char timeformat[80];

   sprintf(file_path, "%s.data", client->id);
   if((client_file = fopen(file_path, "a")) == NULL)
   {
      return -1;
   }
   get_time(timeformat, 80, 1);
   fprintf(client_file, "%s;%s\n", timeformat, client_info);
   fclose(client_file);
   return 0;
}

void msg_system(char *msg)
{
   char timeformat[80];
   get_time(timeformat, 80, 0);
   printf("\r%s MSG => %s.\n", timeformat, msg);
}

void error_system(char *msg)
{
   char timeformat[80];
   get_time(timeformat, 80, 0);
   printf("\r%s Error => %s.\n", timeformat, msg);
}

void debug_message(char *msg)
{
   char timeformat[80];
   if(debug_mode)
   {
      get_time(timeformat, 80, 0);
      printf("\r%s DEBUG => %s.\n", timeformat, msg);
   }
}

void get_time(char *time2format, int size, int all)
{
   time_t t = time(NULL);
   struct tm *tlocal = localtime(&t);
   if(all)
      strftime(time2format, size, "%Y-%m-%d;%H:%M:%S", tlocal);
   else
      strftime(time2format, size, "%H:%M:%S", tlocal);
}


char *get_stat_name(int stat)
{
   if(stat == DISCONNECTED)
      return "DISCONNECTED";
   else if(stat == WAIT_INFO)
      return "WAIT_INFO";
   else if(stat == REGISTERED)
      return "REGISTERED";
   else if(stat == SEND_ALIVE)
      return "SEND_ALIVE";
   else
      return NULL;
}
