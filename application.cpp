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
#define MQTT_PASSWD "senha"
#define MQTT_TOPIC  "beer/temperature"

#define INI_FILE "/flash/program.ini"
#define LOG_FILE "/flash/yevesta.log"
#define LIM_FILE "/flash/limits.ini"

//#define MY_SSID   "Ye VESTA"
//#define MY_PASSWD "yevesta23"

#define MY_SSID   "ssid_do_wifi"
#define MY_PASSWD "senha_do_wifi"

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

static progs programs = {19.0,22.0,1.0,2.0,20.0,22.0};

//! Atribuicao de temperatura minima
double temp_min = programs.fermentation_min;
//! Atribuicao de temperatura maxima
double temp_max = programs.fermentation_max;

Timer procTimer;
Timer checkConn;
Timer ESPlives;
Timer loadLimits;
//inicializador do mqtt
void startMqttClient();
//Apenas declaracao do callback
void onMessageReceived(String topic, String message);
//! Criacao do objeto para comunicacao via MQTT.
MqttClient mqtt(MQTT_BROKER,MQTT_PORT,onMessageReceived);
//lista o sistema de arquivos
void ls();

void sleep(unsigned long interval_ms);


//! Reatribuição da struct programs.
/*! Quando modificados os valores da struct de limites dos programas de 
 * temperatura, é necessário carregá-los. Essa função se encarrega de fazer a
 * reatribuição dos valores.
 */
void split(char *buf, int size){
    char value[5] = {0};
    int j         = 0;
    Vector <String> str_temps;   
    
    for (int i=0;i<size;i++){
        if (buf[i] == '|'){
            value[j] = 0;
            //Adiciona char* ao vetor de strings...
            str_temps.add(value);
            
            //zerar o array que guarda o valor
            for (int k=0;k<5;k++){
                value[k] = 0;
            }
            //reinicia o contador da variavel 'value'
            j = 0;
        }
        else{
            //um,dois,tres, pin...
            value[j] = buf[i];
            j++;
        }
    }
    str_temps.add(value);
    Serial.println("---------------------");
    for (int k=0;k<str_temps.count();k++){
        Serial.println(str_temps.at(k));
    }
    Serial.println("---------------------");
    
    programs.fermentation_min = str_temps[0].toFloat();
    programs.fermentation_max = str_temps[1].toFloat();
    programs.maturation_min   = str_temps[2].toFloat();
    programs.maturation_max   = str_temps[3].toFloat();
    programs.priming_min      = str_temps[4].toFloat();
    programs.priming_max      = str_temps[5].toFloat();
    Serial.println("Novos valores atribuidos.");
}
//! Carrega os valores do arquivo limits.ini
/*! Quando há qualquer modificação em um dos valores do struct programs, um
 * arquivo limits.ini é criado com todos os valores de atribuição do struct
 * programs. Se houver um reboot, os valores predefinidos in flow não serão 
 * perdidos, pois serão caregados desse arquivo. Essa função se encarrega de
 * fazer essa carga. Ela é chamada a partir da primeira função do init
 * (adjustments()).
 */
void strToProg(){
    if (!fileExist(LIM_FILE)){
        Serial.println("Arquivo nao existe. Fui.");
        return;
    }
    Serial.println("Pegando tamanho do arquivo em Bytes...");
    int size = fileGetSize(LIM_FILE);
    char buf[size];
    for (int i=0;i<size;i++){
        buf[i] = 0;
    }
    Serial.println("Abrindo o arquivo de limites...");
    file_t limits_ini = fileOpen(LIM_FILE, eFO_ReadOnly);
    fileSeek(limits_ini,0,eSO_FileStart);
    int size_return   = fileRead(limits_ini,&buf,size);
    Serial.print("size_return: ");
    Serial.println(size_return);
    fileClose(limits_ini);

    Serial.println("Imprimindo valores do arquivo:");
    Serial.print("Valores limites: ");
    Serial.println(buf);
    split(buf,size);
}

//! Converte valores do enumerador @programs em String
/*! Essa conversão é feita para utilização no método @writePrograms(), para
 * carregamento de definições no próximo boot do ESP8266. O caminho reverso é
 * feito pela função strToProg().
 */
String progToStr(){
    String convert = String(programs.fermentation_min);
    convert.concat("|");
    convert.concat(programs.fermentation_max);
    convert.concat("|");
    convert.concat(programs.maturation_min);
    convert.concat("|");
    convert.concat(programs.maturation_max);
    convert.concat("|");
    convert.concat(programs.priming_min);
    convert.concat("|");
    convert.concat(programs.priming_max);
    
    Serial.println("Resultado da conversao:");
    Serial.println(convert);
    return convert;
}

//! Escreve os limites de temperatura no arquivo limits.ini.
/*! Inicialmente o programa é iniciado com predefinições da inicialização do 
 * struct programs. Caso exista em disco o arquivo limits.ini, esses
 * valores são carregados para o enumerador programs com a função de carga
 *  @strToProg().
 * A seleção de um novo valor para qualquer um dos 3 programs é feita no tópico
 * "beer/minMax" e a mensagem deve ter o formato descrito na função
 *  @settingTemps(String toSlice) .
 */
void writePrograms(){
    if (fileExist(LIM_FILE)){
        fileDelete(LIM_FILE);
    }
    sleep(15);
    char buf[40] = {'\0'};
    file_t limits_ini = fileOpen(LIM_FILE,eFO_ReadWrite | eFO_CreateIfNotExist);
    String result = progToStr();
    fileWrite(limits_ini,result.c_str(),result.length());
    fileSeek(limits_ini,0,eSO_FileStart);
    fileRead(limits_ini,&buf,result.length()-1);
    fileClose(limits_ini);
    
}
//! Configuração das temperaturas
/*! Essa função permite mudar as temperaturas pré-definidas para quando o tipo
 * de programa for escolhido, a mínima e máxima desse programa já estejam dentro
 * do padrão desejado.
 * O parâmetro tem que receber o formato MINIMA|MAXIMA|PROGRAMA . Ex.:
 * 13.5|14.5|M
 * Isso dá uma mínima de 13.5, máxima de 14.5 e esses valores são inseridos no
 * programa MATURATION. Os programas são:
 * F (Fermentation)
 * M (Maturation)
 * P (Priming)
 
 Essa função substitui os valores do respectivo programa no enumerador programs.
 */
void settingTemps(String toSlice){
    float tempMin = 0;
    float tempMax = 0;
    
    Serial.println(toSlice);
    //String toSlice = "13.0|17.5|F";
    int first    = toSlice.indexOf('|');
    tempMin      = toSlice.substring(0,first).toFloat();
    
    int last     = toSlice.lastIndexOf('|');
    tempMax      = toSlice.substring(first+1,last).toFloat();
    
    Serial.println("Minima e Maxima:");
    Serial.println(tempMin);
    Serial.println(tempMax);
    Serial.println("----====----====----====----");
    
    char FMP     = toSlice.charAt(toSlice.length()-1);
    
    Serial.print("Nova minima: ");
    Serial.println(tempMin);
    Serial.print("Nova maxima: ");
    Serial.println(tempMax);
    Serial.print("Programa: ");
    Serial.println(FMP);
    
    if (FMP == 'F'){
        programs.fermentation_min = tempMin;
        programs.fermentation_max = tempMax;
        Serial.println("Novos valores atribuidos ao programa Fermentation.");
    }
    else if (FMP == 'M'){
        programs.maturation_min   = tempMin;
        programs.maturation_max   = tempMax;
        Serial.println("Novos valores atribuidos ao programa Maturation.");
    }
    else if (FMP == 'P'){
        programs.priming_min      = tempMin;
        programs.priming_max      = tempMax;
        Serial.println("Novos valores atribuidos ao programa Priming.");
    }
    Serial.println("Não esqueca de escolher o programa de atuacao agora.");
    writePrograms();
}

//! Configuração dos pinos de GPIO
/*! Essa função faz a configuração inicial dos pinos para o modo de boot do
 * ESP8266, assim como configura o modo dos pinos dos relés.
 */
void adjustment(int A, int B, int C){
    //programing = 0 1 0
    //normal     = 1 1 0
    //sd boot    = 0 0 1
    pinMode(0,OUTPUT);
    pinMode(2,OUTPUT);
    pinMode(15,OUTPUT);
    
    pinMode(RELAY_ONE_PIN,OUTPUT);
    pinMode(RELAY_TWO_PIN,OUTPUT);

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

//!Log de excessões.
void logEvent(String msg){
    file_t logF = fileOpen(LOG_FILE, eFO_CreateIfNotExist|eFO_Append);
    if (logF < 0){
        Serial.println("Nao pude criar o arquivo de log.");
        fileClose(logF);
        return;
    }
    fileWrite(logF,msg.c_str(),1);
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
    
    if (fileExist(INI_FILE)){
        Serial.println("Removendo arquivo anterior...");
        fileDelete(INI_FILE);
    }
    
    file_t program_ini = fileOpen(INI_FILE,eFO_ReadWrite | eFO_CreateIfNotExist);
    
    if (program_ini < 0){
        Serial.print("Erro ao abrir arquivo");
        return;
    }
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

//! Leitor do conteúdo de um arquivo
/*! No momento essa função faz exclusivamente a leitura do primeiro byte do
 arquivo de modo de operação (program.ini). Utilizado apenas em debug.
 */
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
    char wrote[40];
    for (int i=0; i<40;i++){
        wrote[i] = 0;
    }
    int size = fileGetSize(fileToRead);
    int content = fileRead(fileChoose,&wrote,size);
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
       /*
       for (int i=0;i<msg.length();i++){
           if (!isdigit(msg.charAt(i)) && msg.charAt(i) != '.'){
           Serial.println("Nao parece temperatura. Abortando...");
           return;
           }
       }
       */
       
       float temp = msg.toFloat();
       Serial.print("Conversao de String para float. Resultado: ");
       Serial.println(temp);
       if (temp <= temp_min){
           //rele off
           digitalWrite(RELAY_ONE_PIN,LOW);
       }
       if (temp >= temp_max){
           //rele on
           digitalWrite(RELAY_ONE_PIN,HIGH);
           sleep(300);
           digitalWrite(RELAY_TWO_PIN,HIGH);
           sleep(1500);
           digitalWrite(RELAY_TWO_PIN,LOW);
       }
   }
   else if (topic == "beer/ls"){
       ls();            
   }
   else if (topic == "beer/ini"){
       readFile(msg);
   }
   else if (topic == "beer/relay"){
       if (msg.charAt(0) == '0'){
           digitalWrite(RELAY_ONE_PIN,HIGH);
       }
       else if (msg.charAt(0) == '1'){
           digitalWrite(RELAY_ONE_PIN,LOW);
       }
       else if (msg.charAt(0) == '2'){
           digitalWrite(RELAY_TWO_PIN,HIGH);
       }
       else if (msg.charAt(0) == '3'){
           digitalWrite(RELAY_TWO_PIN,LOW);
       }
   }
   else if (topic == "beer/minMax"){
       settingTemps(msg);
   }
   else if (topic == "beer/limits"){
       strToProg();
   }
}

//! Reinicia a conexão com o broker em caso de desconexão.
void checkMQTTDisconnect(TcpClient& client, bool flag){
    if (flag == true){
        Serial.println("MQTT Broker Disconnected!!");
    }
    else {
        Serial.println("MQTT Broker Unreachable!!");
    }

    procTimer.initializeMs(2000, startMqttClient).start();
}

//! Apenas publica o status da conexão.
/*! Esse publishing é feito a partir de um timer disparado de dentro da função
 init().
 */
void publishIamLive(){
	if (mqtt.getConnectionState() != eTCS_Connected)
		startMqttClient(); // Auto reconnect

	Serial.println("Publicando status");
	mqtt.publish("freezer/alive", "Alive");
}

//! Inicializador da conexão com o broker.
void startMqttClient(){
    procTimer.stop();
    TcpClientState state_is = mqtt.getConnectionState();
    Serial.println("------------------------");
    Serial.println(state_is);
    Serial.println("------------------------");
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

//! Chamada em caso de falha da conexão com o broker.
void failed(){
    Serial.println("Falha na conexao de rede!");
    //qualquer coisa mais, colocar por aqui.
    logEvent("Falha na conexao de rede");
}

//! Chamada em caso de sucesso na conexão com o broker.
void successful(){
    Serial.println("Conexao bem sucedida. Iniciando MQTT...");
    mqtt.publish("freezer/alive", "Starting");
    startMqttClient();
    ls();
}

//! Configuração inicial da interface sta e desabilitação do modo AP.
/*! Essa função se encarrega de fazer a primeira conexão com a rede WiFi
 * especificada nos defines no inicio do programa. Após confirmada a conexão,
 * o modo AP é desabilitado. Nos resets do ESP8266 essa função não terá mais
 *  efeito. 
 */
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
//! Garantia da conexão constate
/*! Essa função se encarrega de manter a conexão funcional em qualquer situação.
 */
void keepLooking(){
    if (mqtt.getConnectionState() != eTCS_Connected){
        Serial.println("Reconectando!!!");
        startMqttClient();
    }
}

//! Inicializador de execução
void init(){
    spiffs_mount();
    sleep(20);
    adjustment(HIGH,HIGH,LOW);
    
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
    
    checkConn.initializeMs(3000, keepLooking).start();   
    ESPlives.initializeMs(10000, publishIamLive).start();
    //loadLimits.initializeMs(10000, strToProg).startOnce();
}