/************************************************************
 * Fichier : MegaPiAutoTestStepByStepPlusStandard.ino
 * Objet   : Gérer un auto-test ET un scénario standard, 
 *           avec la même structure de code de mouvement.
 * 
 * Partie "Auto-test" : inchangée.
 * Partie "Standard" : on parse les positions par BLE
 *                     puis on appelle trash1(), trash2(), trash3().
 ************************************************************/

#include <HardwareSerial.h>
#include <MeMegaPi.h>
#include <Wire.h>

//yamine est la

struct Trash {
  int row;
  int column;
};

// ============================================================================
// 1) BLOC: Bluetooth & variables globales
// ============================================================================
HardwareSerial &btSerial = Serial3;
String receivedCommand   = "";

// -- Flags & états
bool autoTestInProgress  = false;
bool standardInProgress  = false;
bool isPaused            = false;
bool finished            = false;

// Pour l'auto-test, on a un "currentStep"
int currentStep = -1;

// Notification vers l'IHM
void sendNotification(const String &message) {
  btSerial.print(message);
  btSerial.print("\n");
  Serial.print("[BT] Notification envoyée : ");
  Serial.println(message);
}

// ============================================================================
// 2) Variables & objets pour le robot
// ============================================================================
MeMegaPiDCMotor motorR(PORT3B);
MeMegaPiDCMotor motorL(PORT1B);
MeMegaPiDCMotor motor(PORT2B);
MeMegaPiDCMotor motorPince(PORT4A);

MeGyro gyro;
MeLineFollower line(PORT_5);
MeColorSensor colorSensor(PORT_7);
MeUltrasonicSensor ultraSensor(PORT_8);
//prueba github
// Paramètres globaux
int sensMove           = 1;
bool stateGrab         = true; 
bool stateArm          = false;
bool caseOne           =false;
const char* direction  = "v";
int compteurRow        = 1;
int compteurColumn     = 1;
bool caseLine          = false;
bool beforeCase        = false;
bool onCase            = false;
double targetAngle     = 0;
double currentAngle    = 0;

// ============================================================================
// 3) Définition de la structure Trash + variables auto-test
// ============================================================================


//static const int NUM_CENTERS = 5; 
//String triCenters[NUM_CENTERS] = {
//    "Traditionel",  
//    "Chimique",
//    "Compostable",
//    "Verre",
//    "Recyclable"
//};

// ===== Auto-test : on a 1 trash [Ex. : (3,3)] =====
static const int NUM_TRASHES = 3; 
Trash trashItems[NUM_TRASHES] = {
    {3, 3},  // auto-test
};

// ============================================================================
// 4) Ajout Standard scenario (positions reçues par BLE)
// ============================================================================
static const int STD_MAX_TRASHES = 3;
Trash standardTrashes[STD_MAX_TRASHES];
int   standardCount = 0;  // combien réellement configurés

// Centres “codes à deux lettres” pour Standard (optionnel si vous en avez besoin)
static const int STD_NUM_CENTERS = 5;
String standardCenters[STD_NUM_CENTERS]; 
int   standardCentersCount = 0;

// ============================================================================
// 5) Prototypes (référencés plus bas)
// ============================================================================
void setup();
void loop();

// BLE commands + scenario
void processCommand(const String &command);
void doAutoTestStep();
void doStandardScenario();
void parseStandardTrashes(const String &cmd);
void parseStandardCenters(const String &cmd);

// Fonctions d’arrêt d’urgence
void checkPause();

// Fonctions de navigation & ramassage (inchangées)
void Rotation(double angle);
void correctAngle(double actualAngle,double idealAngle);
void MoveStraightFull(int num,const char* direction);
String takeTrash(const char* direction,int sensMove);
void leaveTrash();
String findTrashCenterByColor(String detectedColor);
int findCT(String centerName);
void OpenGrab();
void CloseGrab();
void armDown();
void armUp();
void armUpDetectTrash();
String colorResult();
void avancerDroit(int time, int sensMove);
void stopRobot(int time);
void degagerLine(int type,int sensMove);

// Fonctions “trash1, trash2, trash3” + tri, etc. (votre code original)
void selectionSort(Trash trashItems[], int number);
void trash1();
void trash2();
void trash3();
bool isRowFree(int row);
bool isColumnFree(int column);
void detection(int trashRow, int trashColumn, String CT);

// ============================================================================
// 6) setup() + loop()
// ============================================================================
void setup() {
  Serial.begin(115200);
  btSerial.begin(115200);

  // Initialisation
  gyro.begin();
  colorSensor.SensorInit();
  Serial.println("MegaPi prête. Attente de commandes via Serial3.");

  // Initialiser standardTrashes
  for(int i=0; i<STD_MAX_TRASHES; i++){
    standardTrashes[i].row    = -1;
    standardTrashes[i].column = -1;
  }
}

void loop() {
  // Lecture BLE
  while (btSerial.available() > 0) {
    char c = btSerial.read();
    if (c == '\n') {
      Serial.print("Commande reçue : ");
      Serial.println(receivedCommand);
      processCommand(receivedCommand);
      receivedCommand = "";
    } else {
      receivedCommand += c;
    }
  }

  // Exécuter l’auto-test si on y est
  if (autoTestInProgress && !isPaused && currentStep >= 0 && !finished) {
    doAutoTestStep();
  }

  // Exécuter Standard
  if (standardInProgress && !isPaused && !finished) {
    doStandardScenario();
  }
}

// ============================================================================
// 7) processCommand() : gère les commandes BLE
// ============================================================================
void processCommand(const String &command) 
{
  if (command == "START_AUTO_TEST") {
    Serial.println("[BLE] START_AUTO_TEST reçu");
    sendNotification("Auto-test:DEMARRAGE");
    autoTestInProgress = true;
    standardInProgress = false;
    isPaused           = false;
    currentStep        = 0;
    finished           = false;

  } else if (command.startsWith("STANDARD:TRASHES=")) {
    parseStandardTrashes(command);

  } else if (command.startsWith("STANDARD:CENTERS=")) {
    parseStandardCenters(command);

  } else if (command == "START_STANDARD") {
    Serial.println("[BLE] START_STANDARD reçu");
    // On commence par ETAPE_0:Debut si vous voulez
    sendNotification("ETAPE_0:Debut");
    standardInProgress = true;
    autoTestInProgress = false;
    isPaused           = false;
    finished           = false;

  } else if (command == "PAUSE") {
    // Met en pause
    if ((autoTestInProgress || standardInProgress) && !isPaused && !finished) {
      isPaused = true;
      sendNotification("Auto-test:PAUSE");
    }
    else {
      Serial.println("[WARN] Scénario inexistant ou déjà en pause");
    }

  } else if (command == "RESUME") {
    // Sort de la pause
    if ((autoTestInProgress || standardInProgress) && isPaused && !finished) {
      isPaused = false;
      sendNotification("Auto-test:RESUME");
    }
    else {
      Serial.println("[WARN] Impossible de RESUME");
    }

  } else if (command == "RESET") {
    autoTestInProgress = false;
    standardInProgress = false;
    isPaused           = false;
    currentStep        = -1;
    finished           = false;
    sendNotification("Auto-test:RESET");
    Serial.println("[INFO] Réinitialisé.");
  } else if (command == "LEVER") {
    autoTestInProgress = false;
    standardInProgress = false;
    isPaused           = false;
    currentStep        = -1;
    finished           = false;
    motor.run(30);
    delay(200);
    motor.stop();
    sendNotification("Lever la pince");
    Serial.println("[INFO] En maintenance.");
    
  } else if (command == "DOWN") {
    autoTestInProgress = false;
    standardInProgress = false;
    isPaused           = false;
    currentStep        = -1;
    finished           = false;
    motor.run(-30);
    delay(200);
    motor.stop();
    sendNotification("Down la pince");
    Serial.println("[INFO] En maintenance.");
  } else if (command == "STOP") {
    autoTestInProgress = false;
    standardInProgress = false;
    isPaused           = false;
    currentStep        = -1;
    finished           = true;
    sendNotification("Auto-test:STOP");
    Serial.println("[INFO] Scénario stoppé.");

  } else {
    Serial.println("[WARN] Commande inconnue => " + command);
    sendNotification("Commande inconnue");
  }
}

// ============================================================================
// 8) doAutoTestStep() : votre code auto-test en étapes
// ============================================================================
void doAutoTestStep() {
  Serial.println("[ETAPE 1] MoveStraightFull(3,'v') + correctAngle");
  sendNotification("ETAPE_1:Debut");
  checkPause();
  degagerLine(S1_IN_S2_IN,1);
  degagerLine(S1_OUT_S2_OUT,1);
  checkPause();
  MoveStraightFull(3,"v");
  checkPause();
  delay(100);
  sendNotification("ETAPE_1:Fin");
  if (finished) return;

  Serial.println("[ETAPE 2] Rotation(89), stopRobot(1000)");
  sendNotification("ETAPE_2:Debut");
  checkPause();
  Rotation(89);
  checkPause();
  stopRobot(1000);
  checkPause();
  sendNotification("ETAPE_2:Fin");
  if (finished) return;

  Serial.println("[ETAPE_3] MoveStraightFull(2,'h'), stopRobot(1000)");
  sendNotification("ETAPE_3:Debut");
  checkPause();
  MoveStraightFull(2,"h");
  checkPause();
  stopRobot(1000);
  checkPause();
  sendNotification("ETAPE_3:Fin");
  if (finished) return;

  Serial.println("[ETAPE_4] Ramassage du déchet => takeTrash('h')");
  sendNotification("ETAPE_4:Debut");
  checkPause();
  String color=takeTrash("h",1);
  checkPause();
  sendNotification("ETAPE_4:Fin");
  if (finished) return;

  Serial.println("[ETAPE_5] MoveStraightFull(5,'h') => Rotation(-90) => MoveStraightFull(5,'v')");
  sendNotification("ETAPE_5:Debut");
  checkPause();
  MoveStraightFull(5,"h");
  checkPause();
  Rotation(-90);
  checkPause();
  MoveStraightFull(5,"v");
  checkPause();
  Serial.println("Arrivé au centre de tri");
  sendNotification("ETAPE_5:Fin");
  if (finished) return;

  Serial.println("[ETAPE_6] leaveTrash()");
  sendNotification("ETAPE_6:Debut");
  checkPause();
  leaveTrash();
  finished=true;
  checkPause();
  sendNotification("ETAPE_6:Fin");
  if (finished) {
    Serial.println("[ETAPE_7] Fin => finished=true");
    sendNotification("Auto-test:TERMINE");
    autoTestInProgress = false;
    finished = true;
  }
}

// ============================================================================
// 9) doStandardScenario() : on copie standardTrashes[] -> trashItems[] 
//    puis on appelle trash1(), trash2(), trash3()
// ============================================================================
void doStandardScenario() 
{
  // Copier standardTrashes[] dans trashItems[] (qui est utilisé par trash1/2/3)
  // Pour éviter d’écraser si vous avez 3 EXACTS. Sinon, testez row=-1 etc.
  for (int i=0; i<STD_MAX_TRASHES; i++){
    if (i < NUM_TRASHES) {
      trashItems[i].row    = standardTrashes[i].row;
      trashItems[i].column = standardTrashes[i].column;
    Serial.print("Trash item ");
    Serial.print(i);
    Serial.print(": row = ");
    Serial.print(trashItems[i].row);
    Serial.print(", column = ");
    Serial.println(trashItems[i].column);
    }
  }

  // Tri s’il le faut
  selectionSort(trashItems, NUM_TRASHES);

  // On appelle successivement trash1, trash2, trash3
  // EXACTEMENT votre code existant
  trash1();
  checkPause();
  if (finished) return;

  trash2();
  checkPause();
  if (finished) return;

  trash3();
  checkPause();
  if (finished) return;

  // A la fin
  finished = true;
  standardInProgress = false;
  sendNotification("Standard:TERMINE");
  Serial.println("[Standard] Terminé.");
}

// ============================================================================
// 10) parseStandardTrashes / parseStandardCenters
// ============================================================================
void parseStandardTrashes(const String &cmd)
{
  // Ex: "STANDARD:TRASHES=2,2;3,4;5,3"
  Serial.println("[Standard] parseStandardTrashes => " + cmd);
  String sub = cmd.substring(strlen("STANDARD:TRASHES="));
  standardCount = 0;
  while(sub.length() > 0 && standardCount < STD_MAX_TRASHES){
    int semi = sub.indexOf(';');
    String item;
    if(semi == -1){
      item = sub; 
      sub="";
    } else {
      item = sub.substring(0, semi);
      sub  = sub.substring(semi+1);
    }
    item.trim();
    if(item.length()>0){
      int comma = item.indexOf(',');
      if(comma>0){
        int row = item.substring(0, comma).toInt();
        int col = item.substring(comma+1).toInt();
        standardTrashes[standardCount].row    = row;
        standardTrashes[standardCount].column = col;
        Serial.print(" => Déchet #"); 
        Serial.print(standardCount+1);
        Serial.print(" = ("); 
        Serial.print(standardTrashes[standardCount].row); 
        Serial.print(","); 
        Serial.print(standardTrashes[standardCount].column); 
        Serial.println(")");
        standardCount++;
      }
    }
  }
}

void parseStandardCenters(const String &cmd)
{
  // Ex: "STANDARD:CENTERS=Tr;Ch;Co;Ve;Re"
  Serial.println("[Standard] parseStandardCenters => " + cmd);
  String sub = cmd.substring(strlen("STANDARD:CENTERS="));
  standardCentersCount = 0;
  while(sub.length()>0 && standardCentersCount < STD_NUM_CENTERS){
    int semi = sub.indexOf(';');
    String item;
    if(semi == -1){
      item = sub; 
      sub="";
    } else {
      item = sub.substring(0, semi);
      sub  = sub.substring(semi+1);
    }
    item.trim();
    standardCenters[standardCentersCount] = item;
    Serial.print(" => Center #");
    Serial.print(standardCentersCount+1);
    Serial.print(" = ");
    Serial.println(item);
    standardCentersCount++;
  }
}

// ============================================================================
// 11) checkPause() : inchangé
// ============================================================================
void checkPause() {
  // Lecture des commandes Bluetooth
  while (btSerial.available() > 0) {
    char c = btSerial.read();
    if (c == '\n') {
      Serial.print("Commande reçue (dans checkPause): ");
      Serial.println(receivedCommand);
      processCommand(receivedCommand);
      receivedCommand = "";
    } else {
      receivedCommand += c;
    }
  }

  if (isPaused) {
    Serial.println("[INFO] Robot en pause. En attente de commande RESUME...");
    sendNotification("Robot:PAUSE");
    // stop immediat
    motorL.stop();
    motorR.stop();
    motor.stop();
    motorPince.stop();

    while (isPaused) {
      if (btSerial.available() > 0) {
        char c = btSerial.read();
        if (c == '\n') {
          Serial.print("Commande reçue (dans pause) : ");
          Serial.println(receivedCommand);
          processCommand(receivedCommand);
          receivedCommand = "";
        } else {
          receivedCommand += c;
        }
      }
      delay(50);
    }

    Serial.println("[INFO] Reprise du robot.");
    sendNotification("Robot:RESUME");
  }

  if (finished) {
    // On peut stopper direct
    motorL.stop();
    motorR.stop();
    motor.stop();
    motorPince.stop();
  }
}

// ============================================================================
// 12) Fonctions EXACTEMENT comme votre code (Rotation, MoveStraightFull, etc.)
//     y compris trash1(), trash2(), trash3()
// ============================================================================

//giroNormal
void Rotation(double angle){
  checkPause();
  if(finished) return;  // si besoin
  if (angle <0){
    if (onCase==true){
      Serial.println("Angle négatif + je suis sur la ligne");
      degagerLine(S1_OUT_S2_OUT,-1);
      beforeCase = true;
      onCase=false;
    } else if(!beforeCase && !onCase){
      Serial.println("Angle négatif + je suis après la ligne");
      degagerLine(S1_IN_S2_IN,-1);
      degagerLine(S1_OUT_S2_OUT,-1);
      beforeCase= true;
    }
  } else {
    if (onCase) {
      degagerLine(S1_OUT_S2_OUT,1);
    }
    if (beforeCase==true){
      Serial.println("Angle positif + avant la ligne");
      degagerLine(S1_IN_S2_IN,1);
      avancerDroit(50,1);
      onCase=true;
      beforeCase=false;
    } else if(!onCase && !beforeCase){
      Serial.println("Angle positif + après la ligne");
      avancerDroit(250,-1);
      onCase=true;
    }
  }
  stopRobot(1000);
  checkPause();
  if(finished) return;

  gyro.begin();
  targetAngle = angle; 
  currentAngle = 0;    

  while (abs(currentAngle) < abs(targetAngle)/2) {
    checkPause();
    if(finished) return;
    if (angle<0){
      motorR.run(100);
      motorL.run(170);
    } else {
      motorL.run(-100);
      motorR.run(-170);
    }
    gyro.update();
    currentAngle = gyro.getAngleZ();
    delay(10);
  }
  while (abs(currentAngle) < abs(targetAngle)){
    checkPause();
    if(finished) return;
    if (angle<0){
      motorR.run(90);
      motorL.run(100);
    } else {
      motorL.run(-80);
      motorR.run(-80);
    }
    gyro.update();
    currentAngle = gyro.getAngleZ();
    delay(10);
  }
  Serial.print("Current Angle: ");
  Serial.println(currentAngle);
  Serial.print("Target angle: ");
  Serial.println(targetAngle);

  correctAngle(currentAngle, targetAngle);
  delay(100);

  int sensorState=line.readSensors();
  if(sensorState==S1_IN_S2_IN){
    onCase=true;
    beforeCase=false;
  } else {
    degagerLine(S1_IN_S2_IN,-1);
    onCase=true;
    beforeCase=false;
  }
  checkPause();
}
//girocorreccion
void correctAngle(double actualAngle,double idealAngle){
  checkPause();
  double diff=-actualAngle-idealAngle;
  Serial.print("angle difference: ");
  Serial.println(diff);
  if (diff < (-0.15)) { // Veering to the right
      if (diff<(-0.15) && diff>=(-0.35)){
        Serial.print("iz1");
        motorL.run(sensMove*30);
        motorR.run(-sensMove*110);
        delay(50);
      }else if (diff<(-0.35)){
        Serial.print("iz2");
        motorL.run(sensMove*30);
        motorR.run(-sensMove*110);
        delay(90);
      }
  } else if(diff>0.2){
      if(diff>0.2 && diff<=0.45){
        Serial.print("der1");
        motorL.run(sensMove*130);
        motorR.run(-sensMove*50);
        delay(190);
      }
      else if (diff>0.45 && diff <=0.8){
        Serial.print("der2");
        motorL.run(sensMove*130);
        motorR.run(-sensMove*50);
        delay(200);
      }
      else if (diff>0.8){
        Serial.print("der3");
        motorL.run(sensMove*130);
        motorR.run(-sensMove*50);
        delay(210);
      }
  }
  checkPause();
  motorL.stop();
  motorR.stop();
}
// moverse
void MoveStraightFull(int num, const char* direction) {
  // --- GESTION PAUSE ---
  checkPause();               // 1) On vérifie d'emblée si on doit se mettre en pause
  if (finished) return;       // 2) Si le scénario est terminé, on sort

  gyro.begin();
  double initialAngle = gyro.getAngleZ();
  Serial.print("Initial Angle: ");
  Serial.println(initialAngle);

  // Déterminer le sens du mouvement (1 pour avancer, -1 pour reculer)
  sensMove = 1;
  if ((strcmp(direction, "h") == 0 && compteurColumn > num) || 
      (strcmp(direction, "v") == 0 && compteurRow > num)) {
      Serial.println("Je vais reculer");
      sensMove = -1;

      // --> ton code pour forcer la ligne
      degagerLine(S1_IN_S2_IN, -1);
      degagerLine(S1_OUT_S2_OUT, -1);
      beforeCase = true;
      onCase     = false;

      checkPause();           // re-check avant de vraiment commencer
      if (finished) return;
  }

  // Dégager la ligne noire
  if (onCase) {
    degagerLine(S1_OUT_S2_OUT, sensMove);
    onCase      = false;
    beforeCase  = false;
  } else if (beforeCase && (sensMove == 1)) {
    degagerLine(S1_IN_S2_IN, 1);
    degagerLine(S1_OUT_S2_OUT, 1);
    onCase      = false;
    beforeCase  = false;
  }

  if ((strcmp(direction, "h") == 0) || (strcmp(direction, "v") == 0)) {
    while (
      (strcmp(direction, "h") == 0 && compteurColumn != num) ||
      (strcmp(direction, "v") == 0 && compteurRow    != num)
    ) {
      checkPause();           // 3) On checkPause() **dans** la boucle
      if (finished) return;

      gyro.update();
      double currentAngle     = gyro.getAngleZ();
      double angleDifference  = currentAngle - initialAngle;

      // Correction d'angle
      if (fabs(angleDifference) > 1.5) {
          if (angleDifference > 0) {
              // Veering right
              if (sensMove == 1) {
                motorL.run(sensMove * 50);
                motorR.run(-sensMove * 130);
              } else {
                motorL.run(sensMove * 150);
                motorR.run(-sensMove * 50);
              }
          } else {
              // Veering left
              if (sensMove == 1) {
                motorL.run(sensMove * 235);
                motorR.run(-sensMove * 40);
              } else {
                motorL.run(sensMove * 50);
                motorR.run(-sensMove * 170);
              }
          }
      } else {
          // Move straight
          motorL.run(sensMove * 80);
          motorR.run(-sensMove * 90);
      }

      int sensorState = line.readSensors();
      if (sensorState == S1_IN_S2_IN) {
        // Incrémenter ou décrémenter le compteur en fonction du sens
        if (strcmp(direction, "h") == 0) {
          compteurColumn += sensMove;
          Serial.print("Compteur Row ");
          Serial.println(compteurRow);
          Serial.print("Compteur Column ");
          Serial.println(compteurColumn);
          degagerLine(S1_OUT_S2_OUT, sensMove);
        } else if (strcmp(direction, "v") == 0) {
          compteurRow += sensMove;
          Serial.print("Compteur Row ");
          Serial.println(compteurRow);
          Serial.print("Compteur Column ");
          Serial.println(compteurColumn);
          degagerLine(S1_OUT_S2_OUT, sensMove);
        }
      }
    }
  }
  //double currentAngle=gyro.getAngleZ();
  // Fin de la boucle => on arrête
  motorL.stop();
  motorR.stop();

  checkPause(); // 4) On peut re-checkPause() avant de sortir
  if (finished) return;
}

String takeTrash(const char* direction, int sensMove){
  checkPause();
  if(finished) return "UNKNOWN";

  degagerLine(S1_IN_S2_IN,sensMove);
  bool detected=false;
  // avancer le robot jusqu'à detection du trash
  while (detected==false && caseOne ==false) {
    checkPause();
    if (finished) return "UNKNOWN";
    Serial.print("Trash not in A colum");
    double distance=ultraSensor.distanceCm();
    Serial.println(distance);
    int sensorState=line.readSensors();
    if (distance < 4) { // Priorité au capteur de distance
      Serial.println("Déchet détecté via capteur ultrason.");
      avancerDroit(400, sensMove); // Avancer pour atteindre le déchet
      detected = true;
    } else if (sensorState == S1_IN_S2_IN) { 
      Serial.println("Déchet détecté via capteur de ligne.");
      if (sensMove==1) avancerDroit(600,-sensMove);
      else if (sensMove==-1) avancerDroit(1100, -sensMove); 
      detected = true;
    } else {
      avancerDroit(15, sensMove);
    }
  }
  if(caseOne == true){
    avancerDroit(400,-1);
    compteurColumn-=1;
  }
  Serial.println("Trash found");
  stopRobot(100);
  checkPause();
  if (finished) return "UNKNOWN";

  armDown();
  checkPause();
  if (finished) return "UNKNOWN";

  CloseGrab();
  checkPause();
  if (finished) return "UNKNOWN";

  armUpDetectTrash(); // => ta fonction custom
  delay(800);         // si besoin
  String detectedColor = colorResult();
  checkPause();
  if (finished) return "UNKNOWN";

  armUp();  // => lève définitivement le bras
  checkPause();
  if (finished) return "UNKNOWN";

  if (detectedColor=="BLACK") {
    // re-check la couleur ?
    detectedColor=colorResult();
  }
  Serial.print("Couleur détectée: ");
  Serial.println(detectedColor);

  // Si standardInProgress => envoi BLE : "STANDARD:DECHET=<COULEUR>"
  if (standardInProgress){
    String msg = "STANDARD:DECHET=" + detectedColor;
    sendNotification(msg);
  } 
  else if (autoTestInProgress){
    String msg = "Auto-test:DECHET=" + detectedColor;
    sendNotification(msg);
  }

  motorL.stop();
  motorR.stop();
  Serial.println("Détection terminée, robot arrêté.");

  degagerLine(S1_IN_S2_IN,1);

  // Mise à jour des compteurs si direction = 'h' ou 'v'
  if(direction == "h" && sensMove==1) compteurColumn+=sensMove;
  else if(direction=="v" && sensMove==1) compteurRow+=sensMove;

  beforeCase=false;
  onCase=true;
  caseOne=false;
  return detectedColor;
}

void leaveTrash(){
  checkPause();
  if(finished) return;

  avancerDroit(700,-1);
  checkPause();
  if(finished) return;

  armDown();
  checkPause();
  if(finished) return;

  OpenGrab();
  checkPause();
  if(finished) return;

  armUp();
  checkPause();
  if(finished) return;

  beforeCase=true;
  onCase=false;
}

String findTrashCenterByColor(String detectedColor){
  checkPause();
  if (finished) return "UNKNOWN";

  if (detectedColor == "WHITE" || detectedColor == "BLACK")  return "Ch";
  if (detectedColor == "YELLOW") return "Co";
  if (detectedColor == "GREEN")  return "Re";
  if (detectedColor == "RED")    return "Tr";
  if (detectedColor == "BLUE")   return "Ve";
  return "Inconnu";
}

int findCT(String centerName) {
  checkPause();
  if(finished) return -1;

  for (int i = 0; i < STD_NUM_CENTERS; i++) {
    if (standardCenters[i] == centerName) {
      return i + 1; 
    }
  }
  return -1; 
}


void OpenGrab(){
  checkPause();
  if(finished) return;

  motorPince.run(80);
  delay(2600);
  checkPause();
  motorPince.stop();
  stateGrab=true;
}

void CloseGrab(){
  checkPause();
  if(finished) return;

  motorPince.run(-100);
  delay(2700);
  checkPause();
  motorPince.stop();
  stateGrab=false;
}

void armDown(){
  checkPause();
  if(finished) return;

  motor.run(-60);
  delay(2250);
  checkPause();
  motor.stop();
  stateArm=false;
}

void armUpDetectTrash(){  // version custom si tu veux
  Serial.print("First going up");
  checkPause();
  if(finished) return;

  motor.run(60);
  delay(1000);
  checkPause();
  motor.stop();
  stateArm=true;
}

void armUp(){
  if (stateArm == true) {
    Serial.print("Second going up after detection");
    checkPause();
    if(finished) return;

    motor.run(60);
    delay(1350);
    checkPause();
    motor.stop();
    stateArm=false;
  }
  else {
    Serial.print("Direct going up ");
    checkPause();
    if(finished) return;

    motor.run(60);
    delay(2350);
    checkPause();
    motor.stop();
  }
}
String colorResult() {
  checkPause();
  int detectedColor = colorSensor.ColorIdentify();
  switch (detectedColor) {
    case BLACK:
      Serial.println("BLACK");
      return "BLACK";
    case BLUE:
      Serial.println("BLUE");
      return "BLUE";
    case YELLOW:
      Serial.println("YELLOW");
      return "YELLOW";
    case GREEN:
      Serial.println("GREEN");
      return "GREEN";
    case RED:
      Serial.println("RED");
      return "RED";
    case WHITE:
      Serial.println("WHITE");
      return "WHITE";
    default:
      Serial.println("UNKNOWN COLOR");
      return "UNKNOWN";
  }
}
void avancerDroit(int time, int sensMove) {
  gyro.update();
  double initialAngle = gyro.getAngleZ();
  unsigned long startTime = millis();

  while (millis() - startTime < time) {
    checkPause();
    gyro.update();
    double currentAngle = gyro.getAngleZ();
    double angleDifference = currentAngle - initialAngle;

    if (abs(angleDifference) > 1) {
      if (angleDifference > 1.5) {
        if (sensMove== 1){
          motorL.run(sensMove*50);
          motorR.run(-sensMove*130);
        } else {
          motorL.run(sensMove*150);
          motorR.run(-sensMove*50);
        }
      } else {
        if (sensMove==1){
          motorL.run(sensMove*240);
          motorR.run(-sensMove*40);
        } else {
          motorL.run(-sensMove*50);
          motorR.run(-sensMove*170);
        }
      }
    } else {
        motorL.run(sensMove*90);
        motorR.run(-sensMove*80);
    }
    delay(10);
  }
  motorL.stop();
  motorR.stop();
  checkPause();
}

void stopRobot(int time){
  checkPause();
  motorL.stop();
  motorR.stop();
  delay(time);
  checkPause();
  
}

void degagerLine(int type,int sensMove){
  int sensorState=line.readSensors();
  while(sensorState != type){
    checkPause();
    avancerDroit(15,sensMove);
    sensorState=line.readSensors();
    delay(10);
  }
  motorR.stop();
  motorL.stop();
}


// ============================================================================
// 13) selectionSort + trash1, trash2, trash3, etc.
// ============================================================================
void selectionSort(Trash tab[], int number) {
  for (int i = 0; i < number - 1; i++) {
    int minIndex = i;
    for (int j = i + 1; j < number; j++) {
      if ((tab[j].row < tab[minIndex].row) ||
          ((tab[j].row == tab[minIndex].row) && tab[j].column < tab[minIndex].column)) {
        minIndex = j;
      }
    }
    Trash tmp = tab[i];
    tab[i] = tab[minIndex];
    tab[minIndex] = tmp;
  }
}

//Traiter le premier dechet dans le cas standard 
void trash1(){
  degagerLine(S1_IN_S2_IN,1);
  degagerLine(S1_OUT_S2_OUT,1);
  int i=0;
  Trash nearestTrash;
  if (trashItems[0].row == trashItems[1].row){
      if (trashItems[0].column<trashItems[1].column) {
        nearestTrash=trashItems[1];
        i=1;
      }
      else nearestTrash=trashItems[0];
      if (nearestTrash.row == trashItems[2].row){
        if (nearestTrash.column<trashItems[2].column) {
          nearestTrash=trashItems[2];
          i=2;
        }
      }
  }else {
    nearestTrash=trashItems[0];
  }
  trashItems[i].row=-1;
  trashItems[i].column=-1;
  Serial.print("Detected trash position");
  Serial.print(nearestTrash.row);
  Serial.print(" , ");
  Serial.print(nearestTrash.column);
  MoveStraightFull(nearestTrash.row,"v");
  Serial.print(compteurRow);
  Serial.print(compteurColumn);
  stopRobot(1000);
  Rotation(90);
  stopRobot(1000);
  if (nearestTrash.column==1) {
    //MoveStraightFull(1,"h");
    caseOne=true;
  }
  else MoveStraightFull(nearestTrash.column-1,"h"); //se place a la colonne avant le dechet
  Serial.print(compteurRow);
  Serial.print(compteurColumn);
  Serial.println(sensMove);
  stopRobot(1000);

  Serial.println("En cours de ramassage...");
  String color=takeTrash("h",1);
  String CT=findTrashCenterByColor(color);
  Serial.print("Déchet détecté de type: ");
  Serial.println(CT);
  int rowCT=findCT(CT);
  Serial.print("Ligne Centre de tri: ");
  Serial.println(rowCT);

  Serial.println("Je suis en train de partir au centre de tri");
  MoveStraightFull(5,"h");
  Rotation(-90);
  MoveStraightFull(rowCT,"v");
  leaveTrash();
}

void trash2(){
  //Déterminer le déchet qui a la colonne la plus proche du centre de tri
  if (beforeCase==true){
    degagerLine(S1_IN_S2_IN,1);
    onCase=true;
    beforeCase=false;
  }
  Trash trash;
  int largestColumnIndex = -1;
  int largestColumn = -1;

  for (int i = 0; i < NUM_TRASHES; i++) {
    if (trashItems[i].column > largestColumn) { 
      // Vérifie que le déchet existe (row != -1) et que la colonne est la plus grande trouvée
      largestColumn = trashItems[i].column;
      largestColumnIndex = i;
    }
  }
  trash=trashItems[largestColumnIndex];
  trashItems[largestColumnIndex].row=-1;
  trashItems[largestColumnIndex].column=-1;
  Serial.println("trash2position");
  Serial.println(trash.row);
  Serial.println(trash.column);
  MoveStraightFull(trash.row,"v");
  Rotation(89);
  Serial.println("En déplacement vers le déchet");
  MoveStraightFull(trash.column,"h");
  if (trash.column==1) caseOne=true;
  Serial.println("En cours de ramassage...");
  String color=takeTrash("h",-1);
  String CT=findTrashCenterByColor(color);
  Serial.print("Déchet détecté de type: ");
  Serial.println(CT);
  int rowCT=findCT(CT);
  Serial.print("Ligne Centre de tri: ");
  Serial.println(rowCT);
  Serial.println("Je vais a la case 4");
  MoveStraightFull(4,"h");
  Serial.println("Je tourne a -90");
  Rotation(-91);
  delay(500);
  Serial.println("Je vais a la colonne du centre de tri");
  MoveStraightFull(rowCT,"v");
  Serial.println("Je tourne a 89");
  Rotation(89);
  delay(500);
  Serial.println("Je vais a la colonne du ct");
  MoveStraightFull(5,"h");
  Serial.println("Je vais tourner -90 2");
  Rotation(-89);
  delay(500);
  leaveTrash();
}

void trash3(){
  Trash trashToSort;
  for (int i = 0; i < NUM_TRASHES; i++) {
    if (trashItems[i].column != -1 && trashItems[i].row !=-1) { 
      // Vérifie que le déchet existe (row != -1) et que la colonne est la plus grande trouvée
      trashToSort=trashItems[i];
      trashItems[i].row=-1;
      trashItems[i].column=-1;
      break;
    }
  }
  Serial.println("trash 3 position");
  Serial.println(trashToSort.row);
  Serial.println(trashToSort.column);
  if (beforeCase==true){
    degagerLine(S1_IN_S2_IN,1);
    onCase=true;
    beforeCase=false;
  }
  MoveStraightFull(trashToSort.row,"v");
  Rotation(90);
  MoveStraightFull(trashToSort.column,"h");
  if (trashToSort.column==1) caseOne=true;
  Serial.println("En cours de ramassage...");
  String color=takeTrash("h",-1);
  String CT=findTrashCenterByColor(color);
  Serial.print("Déchet détecté de type: ");
  Serial.println(CT);
  int rowCT=findCT(CT);
  MoveStraightFull(4,"h");
  Rotation(-90);
  MoveStraightFull(rowCT,"v");
  Rotation(90);
  MoveStraightFull(5,"h");
  Rotation(-90);
  leaveTrash();
}

void detection(int trashRow, int trashColumn, String CT){
  if (beforeCase==true){
    degagerLine(S1_IN_S2_IN,1);
    onCase=true;
    beforeCase=false;
  }
  MoveStraightFull(trashRow,"v");
  Rotation(90);
  MoveStraightFull(trashColumn,"h");
  Serial.println("En cours de ramassage...");
  String color=takeTrash("h",-1);
  int rowCT=findCT(CT);
  MoveStraightFull(4,"h");
  Rotation(-90);
  MoveStraightFull(rowCT,"v");
  Rotation(90);
  MoveStraightFull(5,"h");
  Rotation(-90);
  leaveTrash();
}
bool isRowFree(int row) {
  for (int i = 0; i < NUM_TRASHES; i++) {
      if (trashItems[i].row == row) {
          return false;  // Un déchet est présent sur la ligne
      }
  }
  return true;  // La ligne est libre
}

bool isColumnFree(int column) {
  for (int i = 0; i < NUM_TRASHES; i++) {
      if (trashItems[i].column == column) {
          return false;  // Un déchet est présent sur la ligne
      }
  }
  return true;  // La ligne est libre
}
