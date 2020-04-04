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
   int random_number;
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
int check_id(char* id);
//UDP
void udp_signalhandler(int signal);
void udp_control();
int open_udp_chanel();
void register_process();


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
      //fflush(socket_udp);
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

int check_id(char* id)
{
   int i;
   for(i = 0; i < num_clients; i++)
   {
      if(strcmp(id, clients[i].id) == 0)
         return 1;
   }
   return 0;
}

//UDP Protocol
void register_process(udp_pdu *package)
{
   if(check_id(package->id))
      printf("id correcta\n");  
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
   udp_pdu package;
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
      size = recvfrom(socket_udp, &package, sizeof(udp_pdu), 0, NULL, NULL);
      printf("Peticion");
      if((pid = fork())< 0)
      {
            printf("No se ha podido crear un proceso para atender la peticion\n");
      }
      else if(pid == 0)
      {
         if(package.package_type == REG_REQ)
            register_process(&package);

         exit(0);
      }
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
