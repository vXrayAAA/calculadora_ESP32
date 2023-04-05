#include <stdio.h>								// Acesso as operacoes de entradas/saidas.
#include <string.h>								// Acesso as manipulacoes de strings. 

#include "freertos/FreeRTOS.h"					// Acesso aos termos.
#include "freertos/task.h"						// Acesso as prioridades da TASK.
#include "freertos/queue.h"
#include "freertos/event_groups.h"              // 

#include "driver/gpio.h"						// Acesso ao uso das GPIOs.
#include "driver/adc.h"							// Modulo conversor ADC. OBSOLETO!

#include "esp_system.h"                         // 
#include "esp_wifi.h"                           // 
#include "esp_event.h"          				// => esp_event_base_t
#include "esp_log.h"							// Acesso ao uso dos LOGs.
#include "esp_http_client.h"                    // 
#include "esp_http_server.h"    				// => httpd_uri_t
#include "esp_tls_crypto.h"						// => esp_crypto_base64_encode
#include "esp_netif.h"                          // 
#include "esp_wifi.h"                           // 

#include "nvs_flash.h"          				// => nvs_flash_init

#include "lwip/err.h"                           // 
#include "lwip/sys.h"                           // 
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"

#include "rom/ets_sys.h"						// Acesso a escala de tempo em micro segundos.

#include "sys/param.h"          				// => MIN()

#include "mqtt_client.h"

/* RTOS */
#define CONFIG_FREERTOS_HZ 100					// Definicao da Espressif. Escala de tempo base (vTaskDelay).

/* GERAL */
#define bitX(valor,bit) (valor&(1<<bit))		// Testa e retorna o 'bit' de 'valor'.
#define bit1(valor,bit) valor |= (1<<bit)		// Faz o 'bit' de 'valor' =1.
#define bit0(valor,bit) valor &= ~(1<<bit)		// Faz o 'bit' de 'valor' =0.

#define keyboardCK	GPIO_NUM_4							                        // Seleciona o pino de 'clock' para o registrador.
#define keyboardWR	GPIO_NUM_16							                        // Seleciona o pino de 'data out' para o registrador.
#define keyboardLD	GPIO_NUM_2							                        // Seleciona o pino de 'load' para o registrador.
#define keyboardRD	GPIO_NUM_15							                        // Seleciona o pino de 'data in' para o registrador.


/* LCD */
#define lcdCK	GPIO_NUM_17                       			                // Seleciona o pino de 'clock' para o registrador.
#define lcdDT	GPIO_NUM_18                       			                // Seleciona o pino de 'data' para o registrador.
#define lcdLD	GPIO_NUM_5                       			                // Seleciona o pino de 'load' para o registrador.
#define RS		GPIO_NUM_2                       			                // Bit do registrador.
#define EN		GPIO_NUM_3                       			                // Bit do registrador.
#define ___tempo	10															// Tempo (ms) para pulso do pino EN.

unsigned char calc[]={0,0,0,0,'\0'}; 
char a,b,c;

unsigned char tecTecNew,tecTecOld;				                                // Var. global para rotina anti-repeticao.



void convASC(unsigned char *valor2Asc)
{
    unsigned char contar;
    for(contar = 0;contar<3;contar++)
    {
        valor2Asc[contar]=valor2Asc[contar]+'0';
    }
}



/**
*	@brief Limpa os registradores do hardware (74HC595). Impede acionamentos indevidos na inicializacao.
*/
void __lcdCls(void)										                        // Limpa o registrador.
{
	unsigned char __tmp0;									                    // Var local temporaria.
	gpio_set_level(lcdDT,0);								                    // Desliga o bit.						
	for(__tmp0=0;__tmp0<8;__tmp0++)                                             // Laco para zerar o registrador.
	{
		gpio_set_level(lcdCK,1);									            // Gera um pulso de clock no registrador.
		gpio_set_level(lcdCK,0);                                             // ...
	}
}

/**
*	@brief Funcao interna: Converte valor paralelo em serial para o 74HC595.
*	@param __vlrL1 Dado para conversao.
*/
void __lcdSerial(unsigned char __vlrL1)					                        // Serializa o dado.
{
	unsigned char tmp1;														// Var local temporaria.
	for(tmp1=0;tmp1<8;tmp1++)												// Laco para serializar.
	{
		if(bitX(__vlrL1,(7-tmp1)))gpio_set_level(lcdDT,1);					// Verifica o valor do bit, se 1...
		else gpio_set_level(lcdDT,0);										// ... e se for 0.				
		gpio_set_level(lcdCK,1);									            // Gera um pulso de clock no registrador.
		gpio_set_level(lcdCK,0);                                             // ...
	}							
	gpio_set_level(lcdLD,1); 							                    // Gera um pulso para carregar o dado.
	gpio_set_level(lcdLD,0);                                                 // ...
}

/**
*	@brief Funcao interna: Converte valor 8bits em 4+4bits + controle do bit RS.
*	@param valor Dado em 8bits para conversao.
*	@param pinoRs Valor do pino RS do LCD, valor: 0(comando) ou 1(dado).
*/
void __lcd1Bit(unsigned char valor, unsigned char pinoRs)                       // Rotina de acesso ao LCD por registrador.
{
	unsigned char tmp0;														// Var local temporaria.
	__lcdCls();																	// Limpa o registrador.
	tmp0= valor & 0xF0;														// Separa a unidade.
	bit1(tmp0,EN);															// Acrescenta o bit '___EN'.
	if(pinoRs)	bit1(tmp0,RS);												// Se dado ___RS=1...
	else		bit0(tmp0,RS);												// ... senao ___RS=0-.
	__lcdSerial(tmp0);														// Serializa o dado.
	vTaskDelay(pdMS_TO_TICKS(___tempo));										// Aguarda...
	bit0(tmp0,EN);															// Remove o bit '___EN' (gerando a borda de descida).
	if(pinoRs)	bit1(tmp0,RS);												// Se dado ___RS=1
	else		bit0(tmp0,RS);												// Senao ___RS=0
	__lcdSerial(tmp0);														// Serializa o dado.
	tmp0=(valor & 0x0F)<<4;													// Separa a dezena e posiciona.
	bit1(tmp0,EN);															// Acrescenta o bit '___EN'.
	if(pinoRs)	bit1(tmp0,RS);												// Se dado ___RS=1
	else		bit0(tmp0,RS);												// Senao ___RS=0
	__lcdSerial(tmp0);														// Serializa o dado.
	vTaskDelay(pdMS_TO_TICKS(___tempo));										// Aguarda...
	bit0(tmp0,EN);															// Remove o bit '___EN' (gerando a borda de descida).
	if(pinoRs)	bit1(tmp0,RS);												// Se dado ___RS=1...
	else		bit0(tmp0,RS);												// ... senao ___RS=0.
	__lcdSerial(tmp0);														// Serializa o dado.
}
/**
*	@brief Funcao interna: Envia comando de posicao do cursor para o LCD.
*	@param linha Linha onde o cursor sera posicionado, valor: 1 ou 2.
*	@param col Coluna onde o cursor sera posicionado, valor: 1 ate 16.
*/
void __lcdPos(unsigned char linha, unsigned char coluna)						// Posiciona o cursor.
{
	if(coluna>16){coluna=16;}													// Limita valor maximo.
	if(linha==1)__lcd1Bit((127+coluna),0);					                    // Se for a 1a. linha...
	if(linha==2)__lcd1Bit((191+coluna),0);					                    // Se for a 2a. linha...
}

/**
*	@brief Inicializa os pinos do hardware e o LCD. Obrigatorio antes de qualquer funcao.
*/
void lcdIniciar(void)									                        // Inicializa o LCD.
{
	gpio_reset_pin(lcdCK);													// Reinicia o pino.
	gpio_set_direction(lcdCK, GPIO_MODE_OUTPUT);								// Configura o pino como saida.
	gpio_reset_pin(lcdDT);													// Reinicia o pino.
	gpio_set_direction(lcdDT, GPIO_MODE_OUTPUT);								// Configura o pino como saida.
	gpio_reset_pin(lcdLD);													// Reinicia o pino.
	gpio_set_direction(lcdLD, GPIO_MODE_OUTPUT);								// Configura o pino como saida.

	__lcd1Bit(0x02,0);															// Habilita o uso em 4 bits.
	__lcd1Bit(0x28,0);															// Habilita  duas linhas, 5x7 e cursor simples.
//	__lcd1Bit(0x0E,0);															// Liga o display e o cursor.
	__lcd1Bit(0x0C,0);															// Liga somente o display.
	__lcd1Bit(0x01,0);															// Limpa memoria do LCD e posiciona em HOME.
}

/**
*	@brief Envia texto para o LCD.
*	@param letras Texto a ser enviado. Exemplo: "Teste".
*	@param linha Linha onde o texto sera posicionado, valor: 1 ou 2.
*	@param col Coluna onde o texto sera posicionado, valor: 1 ate 16.
*/
void lcdTexto(char *letras, unsigned char linha, unsigned char coluna)	        // Envia um texto para o LCD.
{
	__lcdPos(linha,coluna);														// Posiciona o cursor.
	while(*letras)											                    // Enquanto houver caracteres validos...
	{
		__lcd1Bit(*letras,1);								                    // ...envia o caracter e...
		letras++;											                    // ...avanca para o proximo caracter.
	}
}

/**
*	@brief Envia caracter unico para o LCD.
*	@param letra Caracter a ser enviado. Exemplo: 'A' ou 0x30.
*	@param linha Linha onde o caracter sera posicionado, valor: 1 ou 2.
*	@param col Coluna onde o caracter sera posicionado, valor: 1 ate 16.
*/
void lcdCaracter(char letra, unsigned char linha, unsigned char coluna)	        	// Envia um caracter para o LCD.
{
	__lcdPos(linha,coluna);															// Posiciona o cursor.
	__lcd1Bit(letra,1);								                    			// ...envia o caracter.
}

/**
*	@brief Carrega caracter customizado.
*	@param ender Endereco com valor de 0 a 7.
*	@param nome Nome da variavel Matriz com os dados.
*/
void lcdCustom(unsigned char ender, unsigned char *nome)
{
	unsigned char LACO01=0;				// Variavel temporaria para o laco.
	if(ender>0x07){ender=0x07;}				// Previne erro de digitacao.
	__lcd1Bit((0x40+ender),0);				// Envia o endereco da posicao do caracter.
	for (LACO01=0;LACO01<8;LACO01++)	// Laco de carga dos dados.
	{
		__lcd1Bit(nome[LACO01],1);		// Envia o caracter da matriz.
	}
	__lcd1Bit(0x01,0);						// Finaliza processo.
}

unsigned char keyboardScan(void)					                                // Le as linhas.
{
	unsigned char entrada=0x00;				                                    // Var local temporaria para a entrada.
	unsigned char tst=0x00;				                                        // Var local temporaria para o laco.

	gpio_set_level(keyboardLD,1);					                                // Ativa o pino da carga do dado.
	for(tst=0;tst<8;tst++)							                            // Laco para varrer os bits.
	{
		if(gpio_get_level(keyboardRD)==1) bit1(entrada,(7-tst));		            // Se o pino de entrada estiver ativado...
		else			                bit0(entrada,(7-tst));		            // ... senao...
        gpio_set_level(keyboardCK,1);					                            // Gera um pulso de clock no registrador.
        gpio_set_level(keyboardCK,0);					                            //
	}
	gpio_set_level(keyboardLD,0);						                            // Desativa o pino da carga do dado.
	return (entrada);								                            // Retorna com o valor
}

void keyboardSerial(unsigned char vlrC1)				                            // Envia as colunas para os registradores.
{
	unsigned char tmp1;
	for(tmp1=0;tmp1<8;tmp1++)					                            // Laco para serializar.
	{
        if(bitX(vlrC1,(7-tmp1)))gpio_set_level(keyboardWR,1);		            // Verifica o valor do bit, se 1...
        else                        gpio_set_level(keyboardWR,0);		            // ... e se for 0.
        gpio_set_level(keyboardCK,1);					                            // Gera um pulso de clock no registrador.
        gpio_set_level(keyboardCK,0);					                            //
	}
    gpio_set_level(keyboardLD,1);						                            // Gera um pulso para carregar o dado.
    gpio_set_level(keyboardLD,0);						                            //
}

const unsigned char tecladinMatriz[4][4]=			                            // Definicao do valor das teclas.
{
	// {'1','2','3','A'},
	// {'4','5','6','B'},
	// {'7','8','9','C'},
	// {'F','0','E','D'}
    {'1','4','7','F'},
	{'2','5','8','0'},
	{'3','6','9','E'},
	{'A','B','C','D'}
};
void keyboardTest(unsigned char keyTmp, unsigned char matPos)	                    // Verifica e recupera valor na matriz.
{
	if(keyTmp==1) tecTecNew=tecladinMatriz[0][matPos];	                        // Se for a 1ª linha...
	if(keyTmp==2) tecTecNew=tecladinMatriz[1][matPos];	                        // Se for a 2ª linha...
	if(keyTmp==4) tecTecNew=tecladinMatriz[2][matPos];	                        // Se for a 3ª linha...
	if(keyTmp==8) tecTecNew=tecladinMatriz[3][matPos];	                        // Se for a 4ª linha...
    // if(keyTmp==1) tecTecNew=___teclaMatriz[matPos][0];	                        // Se for a 1ª linha...
	// if(keyTmp==2) tecTecNew=___teclaMatriz[matPos][1];	                        // Se for a 2ª linha...
	// if(keyTmp==4) tecTecNew=___teclaMatriz[matPos][2];	                        // Se for a 3ª linha...
	// if(keyTmp==8) tecTecNew=___teclaMatriz[matPos][3];	                        // Se for a 4ª linha...
}

char key(void)							                            // Rotina de varredura e verificacao do teclado.
{
	unsigned char tempKey=0x00;						                        // Var local temporaria com o valor da tecla.
	unsigned char tempCol=0x01;						                        // Var local temporaria com o valor da coluna.
    // unsigned char ___tmpLin=0x01;						                    // Var local temporaria com o valor da linha.
	unsigned char tempPos=0x00;						                        // Var local temporaria com o valor da posicao.

	for(tempPos=0;tempPos<4;tempPos++)			                        // Laco de varredura e verificacao.
	{
		keyboardSerial(tempCol);							                        // Seleciona a coluna.
        // keyboardSerial(tempLin);							                    // Seleciona a linha.
		// vTaskDelay(1);									                        // Aguarda estabilizar.
		tempKey=keyboardScan();							                        // Faz varredura do teclado.
		keyboardTest(tempKey,tempPos);					                        // Verifica o valor se a tecla for acionada.
		tempCol=tempCol<<1;							                        // Proxima coluna.
        // tempLin=tempLin<<1;							                    // Proxima linha.
	}
	keyboardSerial(0x00);									                        // Limpa o registrador de deslocamento.

	/* Codigo anti-repeticao */
	if(tecTecNew!=tecTecOld)	tecTecOld=tecTecNew;	                        // Se o valor atual eh diferente da anterior, salva atual e ... 
	else	tecTecNew=0x00;								                        // ...se nao... Salva como nao acionada ou nao liberada.						
	return (tecTecNew);									                        // ...retorna com o valor.
}

void KeyboardInit(void)							                            // Rotina para iniciar o Teclado.
{
	gpio_reset_pin(keyboardCK);													// Limpa configuracoes anteriores.
	gpio_reset_pin(keyboardWR);													// Limpa configuracoes anteriores.
	gpio_reset_pin(keyboardLD);													// Limpa configuracoes anteriores.
	gpio_reset_pin(keyboardRD);													// Limpa configuracoes anteriores.

    gpio_set_direction(keyboardCK, GPIO_MODE_OUTPUT);								// Configura o pino como saida.
    gpio_set_direction(keyboardWR, GPIO_MODE_OUTPUT);								// Configura o pino como saida.
    gpio_set_direction(keyboardLD, GPIO_MODE_OUTPUT);								// Configura o pino como saida.
    gpio_set_direction(keyboardRD, GPIO_MODE_INPUT);								// Configura o pino como entrada.

	gpio_set_level(keyboardCK,0);													// Limpa o pino.
	gpio_set_level(keyboardWR,0);													// Limpa o pino.
	gpio_set_level(keyboardLD,0);													// Limpa o pino.
	gpio_set_level(keyboardRD,0);													// Limpa o pino.
}


void calculaSoma(char *valor)
{
    unsigned char par=0,impar=0;
    unsigned char contar;
    unsigned int soma;
    
    for(contar = 0; contar<2;contar = contar+2)
    {
        impar = impar + calc[contar];
        
    }
    for(contar = 1; contar<2;contar = contar+2)
    {
        par = par + calc[contar];
        
    }
    
//    par = par * 3;
        
        soma = par + impar;
//        soma = soma %10;
//        soma = 10 - soma;
        valor[2] = soma;
         
}


void app_main(void)
{
    unsigned char tecla, contar=0;  /* Declaração de variáveis. "tecla" armazena a tecla pressionada pelo usuário e "contar" é uma variável de controle. */


   // unsigned char tecla2, contar2=0;

    KeyboardInit(); /*inicia o teclado matricial*/
    lcdIniciar();  /*inicia o display LCD*/
    lcdTexto("calc",1,1); /*aparece a mensagem */
    lcdTexto("a + b = c",2,2);

    while (contar<1)
    {
        tecla = key(); /* Lê a tecla pressionada pelo usuário. */
        if(tecla >= '0' && tecla <='9')
        {
            __lcdPos(2,2);
            a = tecla - 0x30;
            __lcd1Bit(tecla,1);
            contar++;
        }

    }


    while (contar<2)
    {

        tecla = key();
        if (tecla >= '0' && tecla <='9')
        {
            __lcdPos(2,6);
            b = tecla - 0x30;
            __lcd1Bit(tecla,1);
            contar++;

        }

    }
    c = a + b;
    __lcdPos(2,10);
    __lcd1Bit(c + 0x30,1);
 
    while (1)
    {
        
    }

}
