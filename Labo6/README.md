# Laboratoire 6 DRV Développement de drivers kernel-space III
Spinelli isaia
Début : 18.12.19

lien du laboratoire : http://reds-lab.einet.ad.eivd.ch/drv_2019/lab_06/lab_06.html

## Objectifs

- Utiliser les queues en kernel space pour traiter des données
- Apprendre à synchroniser les interactions entre les différentes parties d’un driver


## Matériel nécessaire

Ce laboratoire utilise le même environnement que le laboratoire précédent, qui peut donc être réutilisé.

## Test des programmes

Afin de tester tous les exercices qui suivrons, voici une marche à suivre générale:
1. Compiler le module souhaité avec la commande (Ex : make <Nom_fichier>)
2. Copier le moduler (.ko) sur la DE1 via /export/drv
3. Insérer le module depuis le DE1 avec la commande "insmod <Nom_module>"
4. Une fois terminé, vous pouvez supprimer le module avec la commande "rmmod <Nom_module>"


Remarque :


## Gestion des interruptions

Le mécanisme de gestion des interruptions utilisé jusqu’à présent peut s’avérer inefficace lorsqu’on a des tâches conséquentes à réaliser pour traiter une interruption. Par exemple, si on doit traiter une grande quantité de données suite à la pression d’un bouton, il est évident qu’on ne peut pas effectuer ces opérations dans l’interrupt handler. Il ne faut pas oublier non plus qu’il est **interdit** d’appeler dans un interrupt handler des fonctions qui pourraient s’endormir.

Plusieurs mécanismes sont apparus au cours des années pour combler cette lacune. En particulier, le noyau Linux propose les **tasklets** et les **work-queues**. Vous pouvez trouver davantage d’informations ici : https://developer.ibm.com/tutorials/l-tasklets/


## Exercice


### But

Le but est de implémenter un driver qui utilise les afficheurs 7-segments pour afficher les numéros (entre 0x000000 et 0xFFFFFF) donnés par l’utilisateur en utilisant un device node.

## Explications du fonctionnent

Les numéros, écrits par l’utilisateur sur le device, sont lus par le driver et placés dans une queue de taille choisie par l’utilisateur. Une lecture du device affichera à l’écran la liste des éléments disponibles. La FIFO ne pourra jamais avoir une taille inférieure à 1, et elle est choisie en utilisant les switches SW0 - SW5 et le bouton KEY0. La taille est codifiée par les switches en format binaire, et le bouton permet de valider la nouvelle configuration et changer la taille de la FIFO. La taille en binaire est ajoutée à la valeur minimale (1) pout obtenir la valeur choisie. Par exemple, si SW5-SW0 = « 010011 », la taille choisie lorsque le bouton est poussé sera 20 (19 entrée par l’utilisateur + 1 taille minimale). Au chargement du driver, la valeur est lue sans besoin de pousser sur le bouton KEY0. Chaque taille devra être enregistrée dans une liste dans le noyau. Cette liste devra être accessible (en lecture seule) à travers un deuxième device dans /dev/ (c.a.d., en faisant cat sur ce device, on doit pouvoir lire l’historique des changements de taille depuis le dernier reset).

Si, au moment de changer la taille, la FIFO contiendra plus d’élément que la nouvelle taille permet, les éléments en surplus les plus anciens seront perdus.

Lorsque l’utilisateur pousse sur le bouton **KEY3**, le driver lit les numéros qui se trouvent dans la FIFO et les affiche sur les afficheurs 7-segments au rythme d’une valeur par demi-seconde, et cela jusqu’à épuisement des valeurs. Si la queue est vide, il affichera la valeur 0xFOOD trois fois.

Pendant l’affichage l’utilisateur peut appuyer plusieurs fois sur le bouton **KEY3**, sans aucun effet jusqu’à la fin de l’affichage. Le système affichera ensuite une valeur finale qui correspondra au nombre de fois que le bouton a été poussé (sans compter le démarrage initiale). Cette valeur est affichée en tout le cas (même si elle vaut 0).

Pendant l’affichage, le device n’acceptera pas des valeurs en entrée. Ces valeurs, néanmoins, ne doivent pas être perdus: ils doivent être mis dans la FIFO dès que l’affichage sera terminé.

L’affichage peut être interrompu en tout moment en poussant sur le bouton **KEY2**.

Le bouton **KEY1** est le reset globale: la FIFO doit être vidée, la liste des tailles doit être réinitialisée pour avoir uniquement la taille actuelle, les données en attente sur le device node doivent être jetés, et le système doit attendre au moins 5s avant de reprendre son fonctionnement normale (toute autre input dans cette période est ignoré, y compris d’autres reset et changements de taille (qui seront, par contre, pris en charge à la fin de cette période)).
