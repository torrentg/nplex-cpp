# Requeriment functests

Objectiu: Crear un programa que executi alguns tests funcionals.
          Es busca que el programa també sigui instructiu.

Es crida de la manera següent:

> functests -u user -p pwd -s "server1:host1,server1:host2"

on:
* -h, --help: mostra l'ajuda
* -u, --user: identificador d'usuari (mandatory)
* -p, --pasword: password de l'usuari (mandatory)
* -s, --servers: llista de servidors (separats per comma, mandatory)

Que fa el programa?
* Al arrancar llegeix la configuració (usant getopt_long)
* Configura el client nplex
* Executa els tests read-committed
* Executa els tests repeatable-read
* Executa els tests serializable

En tots els tests s'arranquen 2 transaccions
* tx0: (read-committed + force) emprada per simular activitat externa (commits) en la bdd
* tx1: tipus de transacció a testar

Exemple test read-committed
* tx0 neteja la bdd
* verificar que tx0.rev = tx1.rev
* verificar que tx1.read(x) es null
* tx0 insereix x=1 (fa commit)
* tx1 llegeix x i verifica que es 1
* tx0 insereix x=2 (fa commit)
* tx1 llegeix x i verifica que es 2
* tx1 fa upsert de x = 99
* tx0 insereix x=3
* tx1 llegeix x i verifica que x=99

Fer tests similars amb els altres nivells de isolation.
Prefereixo molts tests curts, que no un test llarg, encara que calgui rentar la bdd cada cop.
Una única unitat de compilació -> examples/functests.cpp
