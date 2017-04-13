#include <user_config.h>
#include <SmingCore/SmingCore.h>
//#include <SystemClock.h>

/*! \brief Controlador de temperaturas para fases da cerveja
 *         Controle de fermentacao, maturacao e priming.
 * 
 * A producao da cerveja possui 3 fases diferentes, nas quais
 * as temperaturas precisam ser ajustadas. Esse programa em um
 * ESP8266 com 2 reles ajuda nesse controle, dando um feedback
 * via MQTT.
 */

//-------------------- DEFINES ------------------//
#define MQTT_ID     "ElectroDragon"
#define MQTT_BROKER "192.168.1.2"
#define MQTT_PORT   1883
#define MQTT_USER   "dobitaobyte"
#define MQTT_PASSWD "fsjmr112!"
#define MQTT_TOPIC  "beer/temperature"

#define INI_FILE "/flash/program.ini"
#define LOG_FILE "/flash/yevesta.log"

#define MY_SSID   "SuhankoFamily"
#define MY_PASSWD "fsjmr112"

#define RELAY_ONE_PIN 12
#define RELAY_TWO_PIN 13
//-----------------FIM DEFINES-------------------//

/*! O arquivo program.ini devera conter 1 byte, sendo:
 * F - para Fermentacao
 * M - para Maturacao
 * P - para Priming
 * Sempre que o programa for iniciado, a primeira tarefa
 * sera verificar a existencia de um programa (mais adiante 
 * serao vistos detalhes). Se o arquivo de programa existir,
 * seu conteudo alimenta essa variavel program_to
 */
String program_to = "M"; //!< Variavel que recebe a flag do estilo de programa

//! Valores de maxima e minima para as temperaturas
/*! As variaveis temp_max e temp_min sao alimentadas conforme a definicao das
 * variaveis abaixo, conforme a fase selecionada no celular via MQTT. 
*/
struct progs{
    double fermentation_min; /*!< Temperatura minima para fermentacao */
    double fermentation_max; /*!< Temperatura maxima para fermentacao */
    double maturation_min; /*!< Temperatura minima para maturacao */
    double maturation_max; /*!< Temperatura maxima para maturacao */
    double priming_min; /*!< Temperatura minima para priming */
    double priming_max; /*!< Temperatura maxima para priming */
};

static progs programs = {19.0,22.0,1.0,2.0,20.0,22.0}; /*! Atribuicoes*/

//! Atribuicao de temperatura minima
double temp_min = programs.fermentation_min;
//! Atribuicao de temperatura maxima
double temp_max = programs.fermentation_max;

Timer procTimer;
//inicializador do mqtt
void startMqttClient();
//Apenas declaracao do callback
void onMessageReceived(String topic, String message);
//! Criacao do objeto para comunicacao via MQTT.
MqttClient mqtt(MQTT_BROKER,MQTT_PORT,onMessageReceived);
//lista o sistema de arquivos
void ls();

void adjustment(int A, int B, int C){
    //programing = 0 1 0
    //normal     = 1 1 0
    //sd boot    = 0 0 1
    pinMode(0,OUTPUT);
    pinMode(2,OUTPUT);
    pinMode(15,OUTPUT);

    digitalWrite(0,A);
    digitalWrite(2,B);
    digitalWrite(15,C);
}

//! Função de sleep sem delay no hardware
/*! Essa função faz o delay sem causar paralização de hardware, de modo que
 * nunca acontecerá um reset por WDT (Watch Dog Timer), pois as tarefas  de
 * background continuam funcionando livremente (toda a parte relacionada ao
 * wifi, por exemplo).
 * O parâmetro recebido é o intervalo desejado em milisegundos. Dentro da 
 * função pode-se reparar que há um delayMilliseconds(1), que é 1ms de delay
 *  por loop, para que o processador não seja ocupado integralmente na execução
 *  do loop.
 * Atenção: Utilizar sleep() é a melhor forma de garantir que não haverá uma 
 * indisponibilidade de recursos para a tarefa de background, o que poderia 
 * ocasionar um reset do WDT (Watch Dog Timer).
 */
void sleep(unsigned long interval_ms){
    unsigned long initial_time = millis();
    unsigned long delta        = millis()-initial_time;
    
    while (delta < interval_ms){
        delta = millis()-initial_time;
        delayMilliseconds(1); //apenas para a CPU 'respirar'
    }
}

void logEvent(String msg){
    file_t logF = fileOpen(LOG_FILE, eFO_CreateIfNotExist|eFO_Append);
    if (logF < 0){
        Serial.println("Nao pude criar o arquivo de log.");
    }
    fileWrite(logF,msg.c_str(),1);
    char data[5]      = "";
    fileClose(logF);
    
    Serial.println("Arquivo yevesta.log criado com sucesso");
    Serial.println("Reiniciando agora...");
    sleep(300);
    //System.restart();
}
//! Se existir, carrega o valor do arquivo program.ini.
/*! Quando existe o arquivo program.ini, le o conteudo para a variavel
 program_to .*/
void programLoader(){
    char flag;
    if (fileExist(INI_FILE)){
        file_t inifile = fileOpen(INI_FILE,eFO_ReadOnly);
        fileSeek(inifile,0,eSO_FileStart);
        fileRead(inifile,&flag,1);
        fileClose(inifile);
        //program_to = fileGetContent(INI_FILE);
        //TODO: check file stat
        if (flag == 'F'){
            temp_min = programs.fermentation_min;
            temp_max = programs.fermentation_max;
            
            Serial.println("Programa carregado: FERMENTATION");
            Serial.print("Minima: ");
            Serial.println(programs.fermentation_min);
            Serial.print("Maxima: ");
            Serial.println(programs.fermentation_max);
        }
        else if (flag == 'M'){
            temp_min = programs.maturation_min;
            temp_max = programs.maturation_max;
            
            Serial.println("Programa carregado: MATURATION");
            Serial.print("Minima: ");
            Serial.println(programs.maturation_min);
            Serial.print("Maxima: ");
            Serial.println(programs.maturation_max);
        }
        else if (flag == 'P'){
            temp_min = programs.priming_min;
            temp_max = programs.priming_max;
            
            Serial.println("Programa carregado: PRIMING");
            Serial.print("Minima: ");
            Serial.println(programs.priming_min);
            Serial.print("Maxima: ");
            Serial.println(programs.priming_max);
        }
        else{
            logEvent("O arquivo ini esta vazio ou corrompido.");
            Serial.println("Arquivo ini vazio ou corrompido.");
        }  
    }
}

//! Escolha da fase
/*! Quando publicado por MQTT o programa desejado (sendo Fermentation, 
 Maturation ou Priming), o callback onMessageReceived(String topic, String msg)
 *  se encarrega de chamar essa função. O parâmetro recebido é o tipo escolhido,
 sendo as opções 'F' para fermentação, 'M' para maturação e 'P' para priming.*/
void defineProgram(String flag){
    char data[5] = "";
    int chk      = -1;
   
    /*
    file_t program_ini;
    program_ini = fileOpen(INI_FILE,eFO_WriteOnly);
    fileWrite(program_ini,&flag,1);
    fileClose(program_ini);
    */
    //Serial.println("----------- fileSetContent()");
    //fileSetContent(INI_FILE,flag);
    
    if (fileExist(INI_FILE)){
        Serial.println("Removendo arquivo anterior...");
        fileDelete(INI_FILE);
    }
    
    //Serial.println("eFO_CreateNewAlways | eFO_WriteOnly");
    //program_ini = fileOpen(INI_FILE,eFO_CreateNewAlways | eFO_WriteOnly);
    file_t program_ini = fileOpen(INI_FILE,eFO_ReadWrite | eFO_CreateIfNotExist);
    
    if (program_ini < 0){
        Serial.print("Erro ao abrir arquivo");
        return;
    }
    //char teste = 0x46; //F
    char teste = flag.charAt(0);
    fileWrite(program_ini,&teste,1);
    
    fileSeek(program_ini,0,eSO_FileStart);
    char wrote;
    int content = fileRead(program_ini,&wrote,1);
    Serial.print("Content: ");
    Serial.println(content);
    Serial.print("Arquivo contem: ");
    Serial.println(wrote);
    
    fileClose(program_ini);
    
    programLoader();
}

void readFile(String fileToRead){
    ls();
    Serial.println("Abrindo arquivo...");
    file_t fileChoose = fileOpen(fileToRead,eFO_ReadOnly);
    if (fileChoose == -10002){
        Serial.println("Arquivo nao encontrado. Conteudo existente:");
        ls();
        return;
    }
    else if (fileChoose == -10010){
        Serial.println("Descritor de arquivo incorreto");
        ls();
        return;
    }
    char result[2] = {'\0'};
    Serial.println("Lendo conteudo...");
    
    fileSeek(fileChoose,0,eSO_FileStart);
    char wrote;
    int content = fileRead(fileChoose,&wrote,1);
    Serial.print("Conteudo do arquivo: ");
    Serial.println(wrote);
    fileClose(fileChoose);
}

//! Callback do MQTT
/*! Essa funcao é utilizada como callback da comunicação MQTT.*/
void onMessageReceived(String topic,String msg){
   Serial.println(topic);
   Serial.println(msg);
   Serial.print("Temperaturas do programa atual: ");
   Serial.println(temp_min);
   Serial.println(temp_max);
   
   if (topic == "beer/program"){
       if (msg.charAt(0) == 'F'){
           defineProgram("F");
       }
       else if (msg.charAt(0) == 'M'){
           defineProgram("M");
       }
       else if (msg.charAt(0) == 'P'){
           defineProgram("P");
       }
       else{
           Serial.print("Mensagem invalida: ");
           Serial.println(msg);
           logEvent("Mensagem invalida no topico beer/program");
       }
   }
   else if (topic == "beer/temperature"){
       for (int i=0;i<msg.length();i++){
           if (!isdigit(msg[i]) && msg[i] != '.'){
               Serial.println("Nao parece temperatura. Abortando...");
               return;
           }
           float temp = msg.toFloat();
       }
   }
   else if (topic == "beer/ls"){
       ls();            
   }
   else if (topic == "beer/ini"){
       readFile(msg);
   }
}

void checkMQTTDisconnect(TcpClient& client, bool flag){
    if (flag == true){
        Serial.println("MQTT Broker Disconnected!!");
    }
    else {
        Serial.println("MQTT Broker Unreachable!!");
    }

    procTimer.initializeMs(2000, startMqttClient).start();
}

void publishMessage(){
	if (mqtt.getConnectionState() != eTCS_Connected)
		startMqttClient(); // Auto reconnect

	Serial.println("Publicando status");
	mqtt.publish("freezer/alive", "Alive");
}

void startMqttClient(){
    procTimer.stop();
    mqtt.connect("ElectroDragon", MQTT_USER, MQTT_PASSWD);
    //mqtt.setCompleteDelegate(checkMQTTDisconnect);
    mqtt.subscribe("beer/#");
}

//! Lista arquivos no sistema de arquivos SPIFFS
/*! Uma listagem simples, apenas para ver os arquivos disponíveis.*/
void ls(){
    Vector<String> files = fileList();
    Serial.println("Listando arquivos:");
    for (int i=0; i<files.count(); i++){
        Serial.println(files.get(i) + " - " + String(fileGetSize(files.get(i))) + " Bytes");
    }
}

void failed(){
    Serial.println("Falha na conexao de rede!");
    //qualquer coisa mais, colocar por aqui.
    logEvent("Falha na conexao de rede");
}

void successful(){
    Serial.println("Conexao bem sucedida. Iniciando MQTT...");
    mqtt.publish("freezer/alive", "Starting");
    startMqttClient();
    ls();
}

void sta_if(){
    if (!WifiStation.isEnabled()){
        WifiStation.enable(true);
        Serial.println("Ativando interface sta...");
    }
    if (!WifiStation.isConnected()){
        WifiStation.config(MY_SSID,MY_PASSWD,true);
        Serial.println("Conectando ao SSID...");
    }
    if (WifiAccessPoint.isEnabled() && WifiStation.isConnected()){
        WifiAccessPoint.enable(false);
        Serial.println("Desativando AP...");
    }
    
    WifiStation.waitConnection(successful,20,failed);
}

void echo(){
    Serial.print(".");
}

//! Inicializador de execução
void init(){
    adjustment(HIGH,HIGH,LOW);
    spiffs_mount();
    Serial.begin(115200);
    sleep(200);
    Serial.println("Iniciada a serial...");
    sta_if();
       
    String ip_adr = WifiStation.getIP().toString();
    Serial.print("Endereco IP: ");
    Serial.println(ip_adr);
    
    mqtt.connect("ElectroDragon",MQTT_USER,MQTT_PASSWD);
    Serial.println("Iniciada a conexao com o broker MQTT...");
    mqtt.subscribe("beer/#");
    Serial.println("Subscricao para o topico beer/#");
    
    //procTimer.initializeMs(1000, echo).start();
    
    
}

