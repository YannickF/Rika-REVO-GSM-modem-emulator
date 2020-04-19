#include "Arduino.h"
// Le shiel Ethernet utilise les broches
// 10 : SS pour Ethernet
//  4 : SS pour Carte SD
// 50 : MISO
// 51 : MOSI
// 52 : SCK
// 53 : SS  doit être configuré en output, bien que non utilisé par le shield W5100 sinon l'interface SPI ne foncionne pas
//          d'après la doc Arduino officielle
//

// attention, il faut utiliser la librairie Ethernet modifiée (sinon pas de hostname)
// https://github.com/technofreakz/Ethernet/archive/master.zip

#include <Ethernet.h>
#include <SPI.h>
#define IPFIXE    // à commenter pour utiliser le DHCP

// Serial : port USB : affiche les infos sur le fonctionnement
// Serial1 : port RS232 : communique avec le poele RIKA

#define baudUSB 115200
#define baudRIKA 38400
#define DELAI_SAC 25000

#define port_serveur 10005

#define bouton 9
#define led_comm 7
#define led_erreur 5
#define led_sac 3      //13 pour les essais

// PARAMETRES RESEAU ETHERNET
// pour gagner de la place en mémoire, on n'utilisera pas les DNS
// il faut donc fournir l'adresse IP des machines à joindre
//
// Pour augmenter le nombre de sac, on fait une requete http
// http://#IP_JEEDOM#/core/api/jeeApi.php?apikey=#APIKEY#&type=scenario&id=#ID#&action=#ACTION#
// ACTION peut être start, stop, desaction ou activer

byte mac[] = {0xDE,0xAD,0xBE,0xEF,0xFE,0xEF};  // mac address de l'arduino
IPAddress ip(192,168,1,30);                   // si IP fixe pour l'Arduino
IPAddress jeedom(192,168,1,20);               // adresse IP de Jeedom
IPAddress mydns(192,168,1,254);               // adresse IP du DNS
IPAddress mygateway(192,168,1,254);           // adresse IP de la passerelle
IPAddress mymask(255,255,255,0);              // masque réseau
EthernetClient RIKAclient;                      // on crée un client
EthernetClient client;
EthernetServer RIKAserveur = EthernetServer(port_serveur);
// requete HTTP GET à envoyer à la centrale domotique
const char requete[]="GET /core/api/jeeApi.php?apikey=xxxxxxxxxxxxxxxxxx&type=scenario&id=50&action=start HTTP/1.0";
const char requeteDATE[]="GET / HTTP/1.0";  // requete HTTP juste pour obtenir la date


// VARIABLES GLOBALES
String requetePoele= "";
volatile boolean requetePoeleComplete = false;
String requeteUSB="";
volatile boolean requeteUSBComplete = false;
String dataHTTP="";
bool old_b_status = 1;   // status du bouton de la trappe à granulés 1= fermé, 0= ouvert
long chrono_start=0;
long chrono_stop=0;
long duree_ouverture=0;
unsigned char sacs_verses=0;

unsigned char erreur=0;     // numero de l'erreur en cas de commande reçue via HTTP
String sms="NONE";
String last_sms="NONE";
String STATUS="AUCUN STATUS";
const String numtel="+33123456789";
const String codepin="2107";
String jour="70/01/01";
String heure="01:00:00";
char recu;



void clignote(unsigned char led, unsigned char repete, unsigned int delay_on, unsigned int delay_off) {
    unsigned char i;
    for (i=0;i<repete; i++) {
        digitalWrite(led,HIGH);
        delay(delay_on);
        digitalWrite(led,LOW);
        delay(delay_off);
    }
}

void getHTTPdate (void) {
		// obtient la date en analysant la réponse HTTP d'un serveur
		// afin de pouvoir dater à peu près correctement les SMS que le poele lira
		// La date et l'heure sont en effet transmise lors d'une requete AT+CMGR
		// (Il faudrait verifier si une date correcte est vraiment nécessaire pour le poele quand il reçoit un SMS)
	   Serial.print("-> Date AA/MM/JJ: ");
	   dataHTTP.remove(0,11);							// on enleve le nom du jour
	   String day=dataHTTP.substring(0,2);   			// on récupère le jour
	   dataHTTP.remove(0,3);  							// on supprime le jour et l'espace
	   String month = dataHTTP.substring(0,3);  			// on récupère le mois
	   dataHTTP.remove(0,4);  							// on supprime le mois et l'espace
	   String year = dataHTTP.substring(0,4);			// on récupère l'année
	   year.remove(0,2);
       dataHTTP.remove(0,5);								// on supprime l'année et l'espace
       String hour = dataHTTP.substring(0,8);
	   if (month == "Jan") {month="01";}
	   else if (month == "Jan") {month="01";}
	   else if (month == "Feb") {month="02";}
	   else if (month == "Mar") {month="03";}
	   else if (month == "Apr") {month="04";}
	   else if (month == "May") {month="05";}
	   else if (month == "Jun") {month="06";}
	   else if (month == "Jul") {month="07";}
	   else if (month == "Aug") {month="08";}
	   else if (month == "Sep") {month="09";}
	   else if (month == "Oct") {month="10";}
	   else if (month == "Nov") {month="11";}
	   else if (month == "Dec") {month="12";}
	   // on remplace l'heure et le jour avec les nouvelles données
	   jour=year;   // année au format AA et non AAAA
	   jour +="/";
	   jour +=month;
	   jour +="/";
	   jour += day;
	   heure=hour;
       Serial.print(jour);
       Serial.print(" ");
       Serial.println(heure);
}

void send_retour(void) {     // on envoie CR + LF au poele
    Serial1.write(char(13));
    Serial1.write(char(10));
}
void send_OK(void) {        // on envoie OK au poele
    Serial1.print("OK");
    Serial.println("-> Reponse : OK");
    Serial.println();
    send_retour();
}
void send_ERROR(void) {
    Serial1.print("ERROR");
    Serial.println("-> Réponse : ERROR");
    Serial.println();
    send_retour();
}

////////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void inc_nb_sacs(bool demandeDate){
  //on previent jeedom
  //si demandeDate = 0, on ne demande que la date
  //si demandeDate = 1  on fait une vraie requete pour augmenter le nombre de sacs
  if (RIKAclient.connect(jeedom, 80)) {
      Serial.println("-> Connecté à Jeedom");
      if (!demandeDate) {
    	  RIKAclient.println(requeteDATE);  // requete uniquement pour la date
    	  RIKAclient.println();
    	  Serial.println("-> Recupération de la date via requete HTTP.");
    	  dataHTTP="";
    	  while(1)   {          // on récupère la réponse HTTP et on vérifie que c'est un code 200, et que la fin contient OK
    	  	  if (RIKAclient.available()) {
    	  		  char c = RIKAclient.read();
    	  		  //Serial.print(c);
    	  		  //Serial.print(":");
    	  		  if ((c==13) or (c==10)) {
    	  			  //Serial.println(dataHTTP);
    	  			  if (dataHTTP.startsWith("Date")) { getHTTPdate();break;}
    	  			  dataHTTP="";

    			  } else {
      				  dataHTTP += c;
    			  }
    		  }
    	  }
    	  RIKAclient.flush();
    	  RIKAclient.stop();
      }
      else {
    	  RIKAclient.println(requete);  // requete vers le compteur de sacs Jeedom

			  RIKAclient.println();
			  Serial.println("-> Requete transmise.");
			  Serial.print("-> Réponse HTTP : ");
			  dataHTTP="";
			  while(1)   {          // on récupère la réponse HTTP et on vérifie que c'est un code 200, et que la fin contient OK
				  if (RIKAclient.available()) {
					  char c = RIKAclient.read();
					  //Serial.print(c);
					  //Serial.print(":");
					  if ((c==13) or (c==10)) {
						  //Serial.println(dataHTTP);
						  if (dataHTTP.startsWith("Date")) { getHTTPdate();}
						  if (dataHTTP.endsWith("200 OK")) {
							  Serial.println(dataHTTP);
						  }
						  dataHTTP="";
					  } else {
						  dataHTTP += c;
					  }
				  }
				  if (!RIKAclient.connected()) {
					  // on récupère les dernières données (après l'entete HTTP)
					  while (RIKAclient.available()) {
						  char c = RIKAclient.read();
						  dataHTTP += c;
					  }
					  // On affiche les dernières donnees reçues
					  if (dataHTTP.startsWith("ok")) {
						Serial.print("-> Sac pris en compte : ");
					  }
					  else {
						Serial.print("-> ERREUR : sac non pris en compte : ");
					  }
					  Serial.println(dataHTTP);
					  // on ferme la connexion
					  Serial.println("-> Déconnexion.");
					  RIKAclient.flush();
					  RIKAclient.stop();
					  break;
				  }



			  }
      }
  } else {
      Serial.println("Connexion impossible à Jeedom !");
      digitalWrite(led_erreur,HIGH);
      delay(500);
      digitalWrite(led_erreur, LOW);
  }

}
/*
  SerialEvent occurs whenever a new data comes in the
 hardware serial RX.  This routine is run between each
 time loop() runs, so using delay inside loop can delay
 response.  Multiple bytes of data may be available.
 */
void serialEvent1() {
  while (Serial1.available()) {
    // get the new byte:
    char inChar = (char)Serial1.read();
    // add it to the inputString:
    requetePoele += inChar;
    // if the incoming character is a newline, set a flag
    // so the main loop can do something about it:
    if ((inChar == '\n') || (inChar == char(26)) || (inChar == char(13))) {
      requetePoeleComplete = true;
    }
  }
}


void serialEvent() {
  while (Serial.available()) {
    // get the new byte:
    char inChar = (char)Serial.read();
    // add it to the inputString:
    requeteUSB += inChar;
    // if the incoming character is a newline, set a flag
    // so the main loop can do something about it:
    if (inChar == '\n') {
      requeteUSBComplete = true;
    }
  }
}

bool isDIGIT(String chaine) {  // verifie qu'une chaine ne contient que des nombres
  bool reponse = true;
  for (unsigned int i=0;i<chaine.length();i++) {
     reponse = reponse & isDigit(chaine[i]);
  }
  return reponse;
}

void sendEnteteHTTP(unsigned char type) {
	client.println("HTTP/1.1 200 OK");
	if (type==0) {client.println("Content-Type: text/html");}
	if (type==1) {client.println("Content-Type: application/json");}
	client.println("Connection: close");  // the connection will be closed after completion of the response
	client.println();
}
String sendDonneeHTTP(int num_erreur) {
	String json="";
	if (num_erreur) {						// erreur : on transmet le numero de l'erreur au format json
		json="{\"commande\":";
		json += String(num_erreur);
		json += "}";
		client.println(json);
	} else {
		if (dataHTTP=="status") {			// status : on transmet le status du poele, tel que reçu par SMS
			json= json="{\"status\":\"";
			json += STATUS;
			json += "\"}";
			client.println(json);
		}
		else {
			json="{\"commande\":\"OK\"}";  // pas d'erreur : on transmet OK
			client.println(json);
		}
	}
	client.println();
	return json;
}

void setup() {
  // On prépare le port série
  Serial.begin(baudUSB);
  Serial1.begin(baudRIKA);
  Serial.println();
  Serial.println("Démarrage simulateur modem Rika V1.1 ...");
  requetePoele.reserve(254);
  dataHTTP.reserve(512);
  requeteUSB.reserve(10);
  sms.reserve(100);
  // On prépare les Entrées/Sorties
  Serial.print("-> préparation E/S : ");
  pinMode(53, OUTPUT);          // nécessaire pour faire fonctionner le shield Ethernet W5100 sur carte Mega1280 (ou 2560)
  pinMode(4, OUTPUT);           // nécessaire pour désactiver la carte SD du shield Ethernet et activer Ethernet
  digitalWrite(4,HIGH);         // nécessaire pour désactiver la carte SD du shield Ethernet et activer Ethernet
  //pinMode(10, OUTPUT);           // nécessaire pour désactiver le port Ethernet et activer la carte SD
  //digitalWrite(10,HIGH);         // nécessaire pour désactiver le port Ethernet et activer la carte SD
  pinMode(led_comm, OUTPUT);
  pinMode(led_erreur, OUTPUT);
  pinMode(led_sac, OUTPUT);
  pinMode(bouton, INPUT_PULLUP);
  // on met les bons niveaux sur les sorties
  digitalWrite(led_sac,LOW);
  digitalWrite(led_comm,LOW);
  digitalWrite(led_erreur,LOW);
  Serial.println("OK");
  clignote(led_comm,2,100,200);

  // affichage des infos réseau
  Serial.print("-> adresse IP : ");
#ifdef IPFIXE 
  Ethernet.begin(mac,ip,mydns,mygateway,mymask);
  Serial.print(" IP FIXE !");  
  delay(300);     
#else
  if (!Ethernet.begin(mac)) {                                   // adresse IP obtenue par DHCP
    Serial.print(" ERREUR DHCP : impossible de continuer !");       
    while(1) {
      clignote(led_erreur,3,100,250);
      delay(500); 
    }
  }
#endif
  Ethernet.hostName("RIKA");
  Serial.println(Ethernet.localIP());
  Serial.print("-> Masque : ");
  Serial.println(Ethernet.subnetMask());
  Serial.print("-> Passerelle : ");
  Serial.println(Ethernet.gatewayIP());
  Serial.print("-> DNS : ");
  Serial.println(Ethernet.dnsServerIP());
  Serial.print("-> Hostname : ");
  Serial.println(Ethernet.getHostName());
  clignote(led_erreur, 2,100,200);
  delay(1000);
  Serial.print("-> serveur sur port ");
  Serial.print(port_serveur);
  Serial.print(" : ");
  RIKAserveur.begin();
  Serial.println("OK");
  inc_nb_sacs(0);  // on fait une requete juste pour avoir la date
  Serial.println("-> Système prêt !");
  Serial.println();
  clignote(led_sac,2,100,200);
//fin de la boucle setup()
}

void loop() {
    // A-t-on appuyé sur le bouton ?
    bool b_status = digitalRead(bouton);
    if (b_status == 0) {  // trappe ouverte, faut il faire clignoter les LED pour indiquer qu'un sacs va être pris en compte ?
        duree_ouverture = (millis() - chrono_start) / 1000;
        if (duree_ouverture >=20 and duree_ouverture < 35) {
          clignote(led_sac, 1, 100,500);
          delay(500);
        } else if (duree_ouverture >=35 and duree_ouverture <= 60) {
          clignote(led_sac, 2, 100,250);
          delay(500);
        }
    }
    if (b_status != old_b_status) {  // changement d'état
        delay(50);    //petit delai anti rebond
        if (b_status == 0) {
            duree_ouverture = 0;
            old_b_status= b_status;
            chrono_start=millis();
            //Serial.println(chrono_start,DEC);
            Serial.println();
            Serial.println("Ouverture de la réserve ...");
        }
        else {
          old_b_status=b_status;
          Serial.println("-> Fermeture de la réserve ...");
          Serial.print("-> Durée : ");
          chrono_stop = millis();
          //Serial.println(chrono_stop,DEC);
          duree_ouverture = (chrono_stop - chrono_start) / 1000;
          Serial.print(duree_ouverture, DEC);
          Serial.println(" s");
          if (duree_ouverture >=20 and duree_ouverture <35) {
             sacs_verses = 1;
          } else if (duree_ouverture >= 35 and duree_ouverture <=60) {
             sacs_verses = 2;
          }
          else {
            sacs_verses=0;
          }
          if (sacs_verses) {
            Serial.print("-> ");
            Serial.print(sacs_verses);
            if (sacs_verses >1) {Serial.println(" sacs versés");} else {Serial.println(" seul sac versé");}
            for (int i=0; i<sacs_verses; i++) {
                inc_nb_sacs(1);      // on transmet l'info à la centrale domotique (requete HTTP)
            }
          } else {
            Serial.println("-> Aucun sac versé (ouverture trop brève ou trop longue).");
            Serial.println(); 
          }
      }
       
             
       
    }

    //A-t-on reçu une requete venant du poele ?
    if (requetePoeleComplete) {
        Serial.print("Reçu : ");
        Serial.print(requetePoele);
        digitalWrite(led_comm,HIGH);
        delay(100);
        digitalWrite(led_comm,LOW);
        // Il reste maintenant à traiter cette requete
        if (requetePoele.startsWith("AT+CMGS")) {      // le poele veut envoyer un SMS
            // on donne l'invite >
            send_retour();
            Serial1.write(">");
            //Affichage
            Serial.write("-> Envoi SMS ");
            Serial.write("-> Message : ");
            // on récupère le contenu du SMS
            delay(2000); // delai pour laisser un peu de temps au poele pour répondre
            STATUS="";
            recu=0;
            while (recu !=char(26)) {
                if (Serial1.available()) {
                    recu = (char)Serial1.read();
                    if (recu != char(26)) {   // ctrl+z (ASCII 26) pour finir le SMS
                        STATUS+=recu;
                    }
                }
            }
            //Affichage
            Serial.println(STATUS);
            Serial.println("-> +CMGS : 01");
            //reponse
            send_retour();
            Serial1.print("+CMGS : 01");
            send_retour();
            send_OK();
        }
        else if (requetePoele.startsWith("AT+CMGR")) {      // le poele veut lire un SMS
            //affichage
            Serial.print("-> Lecture SMS ");
            Serial.print("-> Message : ");
            Serial.println(sms);
            if (sms != "NONE") {
                //Serial.println();
            	inc_nb_sacs(0); // fausse requete pour obtenir la date réelle
            	Serial.print("-> +CMGR: \"REC READ\",\"");
                Serial.print(numtel);
                Serial.print("\",,\"");
                Serial.print(jour);
                Serial.print(",");
                Serial.print(heure);
                Serial.println("+08\"");
                Serial.print("-> SMS réel : ");
                Serial.print(codepin);
                Serial.print(" ");
                Serial.println(sms);
                // message pour le poele
                send_retour();
                Serial1.print("+CMGR: \"REC READ\",\"");
                Serial1.print(numtel);
                Serial1.print("\",,\"");
                Serial1.print(jour);
                Serial1.print(",");
                Serial1.print(heure);
                Serial1.print("+08\"");
                send_retour();
                Serial1.print(codepin);
                Serial1.print(" ");
                Serial1.print(sms);
                send_retour();
                send_retour();
                send_OK();
                
            } else {                  // le SMS est none : on n'a aucune commande à transmettre
                send_retour();
                send_OK();
            }
        }
        else if (requetePoele.startsWith("AT+CMGD")) {      // le poele veut efface les SMS
            last_sms=sms;
            sms="NONE";
            Serial.print("-> effacement SMS  ");
            Serial.print("-> Message : ");
            Serial.println(sms);
            send_OK();
            
        }
        else if (requetePoele.startsWith("ATE0") or requetePoele.startsWith("AT+CNMI")  or requetePoele.startsWith("AT+CMGF") ) {  // requete de paramètrage : on répond OK sans poser de questions
            send_OK();
        }
        else if (requetePoele!="" && requetePoele!="\n" && requetePoele!="\x1A" && requetePoele!= "\x0D" ) {
           send_ERROR();
           digitalWrite(led_erreur,HIGH);
           delay(500);
           digitalWrite(led_erreur,LOW);
        }
        // on remet tout à zéro pour la prochaine requete
        requetePoele= "";
        requetePoeleComplete = false;
    }

    // A-t-on reçu une requete http sur le serveur ?
    client = RIKAserveur.available();
    if (client) {
        Serial.println("Requete HTTP reçue ...");
        Serial.print("-> Date : ");
        Serial.print(jour);
        Serial.print(" ");
        Serial.println(heure);
        dataHTTP="";
        digitalWrite(led_comm,HIGH);
        delay(200);
        digitalWrite(led_comm,LOW);
        while (client.connected()) {
          if (client.available()) {
              char c = client.read();
              if ((c==13) or (c==10)) {
                  //Serial.println(dataHTTP);
                  if (dataHTTP.startsWith("GET /")) {
                      Serial.println("-> GET ");
                      Serial.print("-> commande : ");
                      dataHTTP.remove(0,5);
                      char i=dataHTTP.indexOf(" ");
                      if (i) {dataHTTP.remove(i);}
                      Serial.println(dataHTTP);
                      // on va réagir en fonction de la commande reçue
                      if (dataHTTP.startsWith("ON"))  {
                        dataHTTP="ON";
                        erreur=0;
                      }
                      else if (dataHTTP.startsWith("OFF"))  {
                        dataHTTP="OFF";
                        erreur=0;
                      }
                      else if (dataHTTP.startsWith("TEL"))  {
                        dataHTTP="TEL";
                        erreur=0;
                      }
                      else if (dataHTTP.startsWith("room"))  {
                        dataHTTP="room";
                        erreur=0;
                      }
                      else if (dataHTTP.startsWith("heat"))  {
                        dataHTTP="heat";
                        erreur=0;
                      }
                      else if (dataHTTP.startsWith("auto"))  {
                        dataHTTP="auto";
                        erreur=0;
                      }
                      else if (dataHTTP.startsWith("status"))  {
                    	dataHTTP="status";
                    	erreur=0;			
                      }
                      else if (dataHTTP.startsWith("r"))  {
                        // on récupère le nombre derrière, et on verifie qu'il est valide
                        dataHTTP.remove(0,1);
                        String nombre=dataHTTP;
                        //Serial.print(nombre);
                        if (isDIGIT(nombre)) {
                            int valeur=nombre.toInt();
                            //Serial.println(valeur);
                            if (valeur < 5) {valeur=5;}
                            if (valeur > 28) {valeur=28;}
                            //Serial.println(valeur);
                            // on reconstruit la commande correctement
                            dataHTTP="r";
                            dataHTTP += String(valeur, DEC);
                            erreur=0;
                        }
                        else {erreur=1;}  // si problème dans le nombre

                      }
                      else if (dataHTTP.startsWith("h")) {
                        // on récupère le nombre derrière, et on verifie qu'il est valide
                        dataHTTP.remove(0,1);
                        String nombre=dataHTTP;
                        //Serial.print(nombre);
                        if (isDIGIT(nombre)) {
                            int valeur=nombre.toInt();
                            //Serial.println(valeur);
                            if (valeur < 30) {valeur=30;}
                            if (valeur > 100) {valeur=100;}
                            valeur=(valeur/5)*5;
                            //Serial.println(valeur);
                            // on reconstruit la commande correctement
                            dataHTTP="h";
                            dataHTTP += String(valeur, DEC);
                            erreur=0;
                        }
                        else {erreur=1;}  // valeur par défaut si problème dans le nombre



                     }
                     else {
                        erreur=2;
                     }
                     if (!erreur) {				// message reçu conforme
                        if (dataHTTP != "status") {  // si la commande n'était pas "status", on envoie un SMS au poele
                        	sms=dataHTTP;
                        	Serial.print("-> SMS à transmettre au poele : ");
                        	Serial.println(sms);
                        }

                        sendEnteteHTTP(1);								// on transmet l'entete
                        Serial.print("-> Réponse HTTP envoyée : ");
                        Serial.println(sendDonneeHTTP(erreur));			// on transmet la réponse
                        Serial.println("-> Déconnexion");
                        client.flush();
                        client.stop();									// on ferme la connexion
                        Serial.println();

                     } else {
                        
                        if (erreur ==1) {
                          Serial.print("-> erreur n°");
                          Serial.print(erreur);
                          Serial.print(" - nombre invalide ");
                          Serial.println(dataHTTP);
                        }
                        else if (erreur ==2) {
                          Serial.print("-> erreur n°");
                          Serial.print(erreur);
                          Serial.print(" - commande invalide ");
                          Serial.println(dataHTTP);
                        }
                        digitalWrite(led_erreur,HIGH);
                        delay(500);
                        digitalWrite(led_erreur,LOW);
                        Serial.println("-> Aucun SMS transmis");
                        sendEnteteHTTP(1);								// on transmet l'entete
                        Serial.print("-> Réponse HTTP envoyée : ");
                        Serial.println(sendDonneeHTTP(erreur));			// on transmet la réponse
                        Serial.println("-> Déconnexion");
                        client.flush();
                        client.stop();									// on ferme la connexion
                        Serial.println();
                     }


                  }
                  dataHTTP="";
              } else {
                  dataHTTP += c;
              }




          }

        }

    }
    client.stop();




    // A-t-on reçu une requete via le port USB ?
    if (requeteUSBComplete) {
        digitalWrite(led_comm,HIGH);
        delay(100);
        digitalWrite(led_comm,LOW);
        if (requeteUSB.startsWith("IP")) {
            Serial.print("L'adresse IP est : ");
            Serial.println(Ethernet.localIP());
            Serial.println();
        }
        else if (requeteUSB.startsWith("SMS")) {
            Serial.println("Le dernier SMS envoyé au poele est :");
            Serial.println(last_sms);
            Serial.println();
        }
        else if (requeteUSB.startsWith("STATUS")) {
            Serial.println("Le dernier STATUS reçu du poele est :");
            Serial.println(STATUS);
            Serial.println();
        }
        else if (requeteUSB.startsWith("SAC")) {
            Serial.println("Ajout d'un sac ...");
            inc_nb_sacs(1);
            Serial.println();
        }
        else
        {
            Serial.println("Menu :");
            Serial.println("IP  -> affiche l'adresse IP");
            Serial.println("SMS -> affiche le dernier SMS envoyé ou reçu");
            Serial.println("SAC -> ajout d'un sac pour Jeedom");
            Serial.println();
            digitalWrite(led_erreur,HIGH);
            delay(500);
            digitalWrite(led_erreur,LOW);
        }

        // on remet le buffer à zéro
        requeteUSBComplete=false;
        requeteUSB="";
    }


// fin de la boucle loop()
}

