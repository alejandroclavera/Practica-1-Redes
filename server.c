#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <string.h>
#include<signal.h>
#include<sys/types.h>
#include<sys/wait.h>

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
}client;

typedef struct
{
   unsigned char package_type;
   char id[13];
   char random_number[9];
   char data[61];

}udp_pdu;

//*********************
//    Funciones
//*********************
int load_configuration(char* path, server_configuration* configuration);
client* read_bbdd(int* num_clients);
client* find_client(char* id);
void package(udp_pdu *package, unsigned char type, char *id, char *rdn, char *data);
//UDP
void udp_signalhandler(int signal);
void udp_control();
int open_udp_chanel();
void register_process(udp_pdu *package, struct sockaddr_in* addr_client, int *laddr_client);


//*********************
//    Variables Locales
//*********************
server_configuration configuration;
client *clients;
int num_clients;
int socket_udp;
int socket_tcp;

void udp_signalhandler(int signal)
{
   if(signal == SIGINT)
   {
      printf("\rProceso cerrado por ^c\n");
      close(socket_udp);
      exit(0);
   }
}

int load_configuration(char* path, server_configuration* configuration)
{
   FILE *file = fopen(path, "r");
   char buffer[1024];
   char *parametre_nom , *parametre_value;
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
         }
         else if(strcmp(parametre_nom, "UDP-port")== 0)
         {
            parametre_value = strtok(parametre_value, "\n");
            configuration->udp_port = atoi(parametre_value);
         }
         else if(strcmp(parametre_nom, "TCP-port")== 0)
         {
            parametre_value = strtok(parametre_value, "\n");
            configuration->tcp_port = atoi(parametre_value);
         }
      }
   }
}

client* read_bbdd(int* num_clients)
{
   client *client_list;
   FILE *file = fopen(CLIENT_BBDD, "r");
   char buffer[1024];
   char *id;
   if(file == NULL) return NULL;
   client_list = (client*)malloc( 20 * sizeof(client));
   *num_clients = 0;
   while(fgets(buffer, 1025, file))
   {
     id = strtok(buffer, "\n");
     if(id != NULL)
     {
         memset(&client_list[*num_clients], 0, sizeof(client));
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

void view_package(udp_pdu * package)
{
   printf("type = %d	\n", package->package_type);
   printf("id = %s	\n", package->id);
   printf("rdn = %s	\n", package->random_number);
}

//UDP Protocol
void register_process(udp_pdu *client_package, struct sockaddr_in* addr_client, int *laddr_client)
{
   client *client_to_register;
   udp_pdu package_to_send;
   int nbytes;
   int rdn;
   int size;
   memset(&package_to_send, 0, sizeof(udp_pdu));
   if((client_to_register = find_client(client_package->id)) == NULL)
   {
      package(&package_to_send, REG_REJ, configuration.id, "00000000", "Id incorrecta");
      nbytes = sendto(socket_udp, &package_to_send, sizeof(udp_pdu),0, (struct sockaddr *)addr_client, *laddr_client);
      if(nbytes < 0)
      {
         fprintf(stderr, "Error al realizar sendto\n");
         exit(-1);
      }
   }
   else if(strcmp(client_package->random_number, "00000000") !=0)
   {
      package(&package_to_send, REG_REJ, configuration.id, "00000000", "numero aleatorio incorrecto");
      nbytes = sendto(socket_udp, &package_to_send, sizeof(udp_pdu),0, (struct sockaddr *)addr_client, *laddr_client);
      if(nbytes < 0)
      {
         fprintf(stderr, "Error al realizar sendto\n");
         exit(-1);
      }
   }
   rdn = rand() % 99999999 + 1;
   sprintf(client_to_register->random_number, "%d", rdn);
	char new_port[100];
   sprintf(new_port, "%d", configuration.udp_port);
   package(&package_to_send, REG_ACK, configuration.id, client_to_register->random_number, "2020");
   nbytes = sendto(socket_udp, &package_to_send, sizeof(udp_pdu),0, (struct sockaddr *)addr_client, *laddr_client);
   if(nbytes < 0)
   {
         fprintf(stderr, "Error al realizar sendto\n");
         exit(-1);
   }
   size = recvfrom(socket_udp, client_package, sizeof(udp_pdu), 0, (struct sockaddr *)addr_client, laddr_client);
   if(strcmp(client_package->id, client_to_register->id) !=0)
   {
      package(&package_to_send, INFO_NACK, configuration.id, client_to_register->random_number, "Id cliente incorrecta");
		nbytes = sendto(socket_udp, &package_to_send, sizeof(udp_pdu),0, (struct sockaddr *)addr_client, *laddr_client);
		exit(0);
   }
   else if(strcmp(client_package->random_number, client_to_register->random_number)!=0) 
   {
      package(&package_to_send, INFO_NACK, configuration.id, client_to_register->random_number, "numero aleatorio incorrectoD");
      nbytes = sendto(socket_udp, &package_to_send, sizeof(udp_pdu),0, (struct sockaddr *)addr_client, *laddr_client);
      exit(0);
   }
   char tcp_port[100];
   sprintf(tcp_port, "%d", configuration.tcp_port);
   package(&package_to_send, INFO_ACK, configuration.id, client_to_register->random_number, tcp_port);
   nbytes = sendto(socket_udp, &package_to_send, sizeof(udp_pdu),0, (struct sockaddr *)addr_client, *laddr_client);
   exit(0);
}

int open_udp_chanel(int *socket_udp)
{
   struct sockaddr_in addr_server;
   if((*socket_udp = socket(AF_INET,SOCK_DGRAM,0)) < 0) 
		return -1;     
   memset(&addr_server, 0, sizeof(struct sockaddr_in));
   addr_server.sin_family = AF_INET;
   addr_server.sin_addr.s_addr = INADDR_ANY;
   addr_server.sin_port= htons(configuration.udp_port);
   if(bind(*socket_udp,(struct sockaddr* )&addr_server,(socklen_t)sizeof(struct sockaddr_in)) < 0) 
      return -1;
   return 0;
}

void udp_control()
{
   struct sockaddr_in addr_client;
   int laddr_client;
   udp_pdu client_package;
   int size;
   int pid;
   if(open_udp_chanel(&socket_udp) == -1)
   {
      fprintf(stderr ,"Error no se ha podido abrir la conexion udp");
      close(socket_udp);
      exit(-1);
   } 
   
   while (1)
   {
      memset(&addr_client, 0, sizeof(struct sockaddr_in));
      size = recvfrom(socket_udp, &client_package, sizeof(udp_pdu), 0, (struct sockaddr *)&addr_client, &laddr_client);
      if((pid = fork())< 0)
      {
            printf("No se ha podido crear un proceso para atender la peticion\n");
      }
      else if(pid == 0)
      {
         if(client_package.package_type == REG_REQ)
            printf("Iniciando proceso de registro\n");
            srand(time(NULL));
            register_process(&client_package, &addr_client, &laddr_client);
      }
      wait(NULL);//temporal
   }
   close(socket_udp);
}

int main(int argc, char *argv[])
{
   signal(SIGINT, SIG_IGN); 
   if(load_configuration(DEFAULT_CONFIGURATION, &configuration) < 0)
   {
      fprintf(stderr,"No se ha podido cargar la configuración.\n");
      exit(-1);
   } 
   if((clients = read_bbdd(&num_clients)) == NULL)
   {
      fprintf(stderr,"No se ha podido cargar los clintes perimitidos.\n");
      exit(-1);
   }

   //Proceso que atienda consexiones UDP
   int udp = fork();
   if(udp<0)
   {
      fprintf(stderr, "Error no se ha podido crear proceso.");
   }
   else if(udp == 0)
   {
      signal(SIGINT, udp_signalhandler);
      udp_control();
   }
   wait(NULL);
}
