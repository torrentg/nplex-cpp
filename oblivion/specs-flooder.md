# Requeriments

Objectiu: Crear un programa per fer proves de rendiment de nplex.

Es crida de la manera següent:

> flooder -u user -p pwd -s "server1:host1,server1:host2" -sps 400

on:
* -h, --help: mostra l'ajuda
* -u, --user: identificador d'usuari (mandatory)
* -p, --pasword: password de l'usuari (mandatory)
* -s, --servers: llista de servidors (separats per comma, mandatory)
* -r, --refresh: temps entre actualització estadístiques (1s per defecte)
* -nk, --num-keys: numero aproximat de claus gestionades (100 per defecte)
* -ds, --data-size: tamany mig d'una entrada (en bytes, 25 per defecte)
* -mat, --max-active-tx: maxim nombre de tx actives (100 per defecte)
* -sps, --submits-per-second: número de submits per segon (0 per defecte)

Que fa el programa?
* Al arrancar llegeix la configuració (usant getopt_long)
* El texte d'ajuda (opció -h) també detalla el format de sortida
* Configura el client nplex:
  * Passant-li directament el user, password i servers passats per paràmetre
  * Li afegeix logger nivell warning (usar logger de examples)
  * No li posa manager
* Li posa un reactor que acumula estadístiques del número de creates, updates i deletes
* Arranca el client
* Crear N tx per segon i fer submit
* Recullir el resultat dels submits i acumular estadistiques
* Cada X segons mostrar les estadistiques i resetejar-les

Observacions:
* Tot en anglés (noms de variables, comentaris, txt de sortida, etc)
* Una única unitat de compilació (*.cpp) situada en la carpeta examples
* Les estadístiques es calculen per unitat de refresc (pe. cada segon)
* Les estadístiques es refresquen cada unitat de refresc
* Les dades que s'asignaran a les claus son de tipus texte (random)
* Fer una funció per generar texte random tenint en compte els arguments del programa (data-size)
* Al rebre el snapshot, considerar les claus existents
* Si no hi ha prous claus, crear-les (veure regles de creació més endavant).
* Per gestionar el flux de submits sugereixo de crear un array de transaccions de tamany min(max-active-tx, submits-per-second). Fer submit de les transaccions del array. Esperar que la alguna respongui. Tenir en compte el temps per poder fer refresh. Acumular estadístiques. Reemplaçar per una nova tx si encara no s'ha satisfet submits-per-second.
* S'executa indefinidament fins que el usuari premi ctrl-C

La sortida per consola del programa serà similar a aquesta:

TIME      #submits #accepted #rejected #committed mintime maxtime avgtime #updates #updkeys #updbytes
------------------------------------------------------------------------------------------------------
hh:mm:ss    143       125       18        125       205    1153     583     188      1502     40Kb

On
* Es mostra una linia per cada unitat de temps de refresc
* Es mostra l'informació en format tabular correctament aliniada
* #submits: numero de submits fets durant el periode
* #accepted: número de tx acceptades (submitCode=ACCEPTED) durant el periode
* #rejected: número de tx rejectades (submitCode!=ACCEPTED) durant el periode
* #committed: numero de tx commitades (s'ha rebut l'update) durant el periode
* mintime: temps de resposta minim en concloure una tx (en microsegons)
* maxtime: temps de resposta màxim en concloure una tx (en microsegons)
* avgtime: temps mig de resposta en concloure una transacció (en microsegons)
* #updates: número de updates rebuts en el periode (estadistica acumulada pel reactor)
* #updkeys: número de claus actualitzades en el periode (estadistica acumulada pel reactor)
* #updbytes: numero de bytes (de les dades) actualitzats en el periode (estadistica acumulada pel reactor)

Per la cració de claus tingues en compte el següent diccionari xxx-[yyy_1, ..., yyy_n]:
* sensor: value, unit, voltage, current, status, range, precision, offset, calibration, timestamp
* actuator: pressure, state, active, position, torque, speed, power, override, fault, control
* alarm: state, priority, active, acknowledged, triggered, reset, type, timestamp, source, message
* counter: value, increment, decrement, reset, overflow, limit, status, timestamp, rate, cycles
* controller: mode, setpoint, output, input, status, error, gain, range, override, fault
* valve: position, flow, pressure, state, status, override, fault, control, open, close
* motor: speed, torque, current, voltage, power, status, temperature, efficiency, fault, control
* pump: flow, pressure, speed, power, status, temperature, efficiency, fault, control, override
* generator: voltage, current, frequency, power, load, efficiency, status, fault, temperature, speed
* transformer: voltage, current, power, temperature, efficiency, status, fault, load, tap, oil
* relay: state, voltage, current, power, trips, resets, cycles, fault, control, override
* switch: state, position, voltage, current, power, fault, control, override, trips, resets
* meter: voltage, current, power, energy, frequency, status, fault, load, efficiency, timestamp
* indicator: status, value, range, precision, fault, power, brightness, color, mode, timestamp
* thermostat: temperature, setpoint, mode, status, fault, override, control, efficiency, power, cycles
* compressor: pressure, temperature, speed, power, status, efficiency, fault, load, control, override
* turbine: speed, power, temperature, pressure, efficiency, status, fault, load, control, override
* heater: temperature, power, status, fault, control, override, efficiency, load, cycles, mode
* fan: speed, power, status, fault, control, override, efficiency, load, temperature, pressure
* scanner: status, mode, efficiency, cycles, resets, load, control, override, power, fault

Crea les claus de la forma següent:
* xxx.t<num>.yyy_k
* on xxx és una de les claus del diccionari
* on <num> és un número (min=1)
* yyy_k es un dels valors (aleatori) de la clau xxx
* Exemple: pump.t78.control
