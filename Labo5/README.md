# Laboratoire 5 DRV Développement de drivers kernel-space II
Spinelli isaia
Début : 20.11.19

lien du laboratoire : http://reds-lab.einet.ad.eivd.ch/drv_2019/lab_05/lab_05.html#

## Objectifs

- Savoir gérer threads, timers et interruptions en kernel-space
- Savoir utiliser les queues en kernel-space
- Apprendre à utiliser les symboles exportés par d’autres modules
- Savoir gérer les échanges d’informations entre le user-space et le kernel-space (command line parameters, sysfs, …)
- Connaître les mécanismes principaux de synchronisation

## Matériel nécessaire

Ce laboratoire utilise le même environnement que le laboratoire précédent, qui peut donc être réutilisé.

## Test des programmes

Afin de tester tous les exercices qui suivrons, voici une marche à suivre générale:
1. Compiler le module souhaité avec la commande (Ex : make <Nom_fichier>)
2. Copier le moduler (.ko) sur la DE1 via /export/drv
3. Insérer le module depuis le DE1 avec la commande "insmod <Nom_module>"
4. Une fois terminé, vous pouvez supprimer le module avec la commande "rmmod <Nom_module>"

Pour les exercices, les attributs dans le sysfs est dans : /sys/kernel/<Nom_Ex>/ (Ex : /sys/kernel/Synch_ex5/).

Pour le dernier exercice refait (Synch_Ex5_clean.c) les attributs sont dans : /sys/devices/platform/ff200000.drv/mydrv/<attribut>

Remarque :
Afin de faire passer ma structure privée dans les diverses fonctions "kobject _**show** ou N _**store**" j'ai essayé d'utiliser la super macro "container_of" qui permet de récupérer l'adresse de la structure privée via un pointeur sur un champs de cette structure.
Malheureusement, avec une structure de type "struct kobject" je n'y suis pas parvenu car c'est vite complexe avec le ktype.

Après avoir terminé tous les exercices, Roberto Rigamonti m'a aidé pour me montrer une méthode qui permet de passer une structure privée dans les fonctions SHOW et STORE dans le sysfs. Afin de maitriser cette méthode sans devoir tout refaire, j'ai refait seulement le dernier exercice avec la bonne méthode (**Synch_Ex5_clean.c**)

## KThreads et timers

D’autres fonctionnalités que l’on retrouve également dans l’espace noyau sont les threads et les timers. Il est possible pour un module de démarrer des tâches d’arrière plan, d’effectuer un polling sur un périphérique à intervalle régulière, ou encore de déléguer le traitement d’une interruption à un thread afin de ne pas rester dans une routine d’interruption trop longtemps.

Un kthread se démarre avec la fonction kthread_run. Il peut ensuite se terminer de deux manières différentes:

- De manière spontanée, en appelant do_exit puis en retournant la valeur de retour de cette fonction.
- Lorsque kthread_stop a été appelé depuis un autre thread.

kthread_stop ne tue pas le thread directement, mais active un flag partagé indiquant que le thread doit s’arrêter, et bloque en attendant que le thread se termine. Ce flag est accessible comme retour de la fonction kthread_should_stop, et le thread doit contrôler périodiquement cette valeur s’il s’attend à être stoppé.

Un thread peut être mis en sommeil grâce à la macro wait_event_interruptible, qui prend comme paramètre une queue sur laquelle le thread attend, et une condition qui doit être respectée pour que ce thread se réveille. Dans l’espace utilisateur, ceci correspondrait à un appel à pthread_cond_wait. Le thread peut ensuite être réveillé via un appel à wake_up_interruptible, prenant comme paramètre la queue de threads à réveiller. Dans le modèle POSIX, ceci équivaudrait à pthread_cond_broadcast.

Un module peut également faire appel aux timers s’il désire effectuer une tâche cyclique, ou compter précisément un intervalle de temps. L’interface des timers a beaucoup changé ces derniers temps, et c’est assez difficile trouver des exemples qui fonctionnent (la plupart ne compile même pas!). Vous pouvez trouver un bon tutoriel ici : https://developer.ibm.com/tutorials/l-timers-list/

### Exercice 1 : Compte à rebours

Le but est de réaliser un module pour la DE1-SoC, pilotant un périphérique de type char, permettant d’utiliser les afficheurs 7-segments pour afficher un compte à rebours. Ce compte à rebours sera animé par un timer dans le noyau.

Une écriture sur le device node associé permettra de choisir un temps initial (en secondes), une lecture permettra de récupérer le temps écoulé dans l’espace utilisateur.

Un fois arrivé à zéro, les LEDs devront clignoter pendant 3s.

Une pression sur le bouton plus à gauche de la carte permettra de démarrer ou de stopper le compte à rebours, alors que une pressions sur le bouton plus à droite permettra de réinitialiser le compteur à sa valeur initiale.

Le code est en annexe (compte_rebours.c).

## Queues (KFIFOs)

Un type de structure de données très souvent utilisé lorsque l’on discute avec un vrai dispositif est la queue, aussi appelée KFIFO. Le noyau offre une interface qui vous permet facilement d’insérer et de retirer des éléments. L’interface est détaillée ici : https://www.kernel.org/doc/htmldocs/kernel-api/kfifo.html
De plus, des exemples d’utilisation sont disponibles dans le sous-répertoire samples des sources du noyau.

### Exercice 2 KFIFOs

Le but est d'implémenter un module du noyau qui, au chargement, remplit une KFIFO avec les premiers N nombres premiers (N <= 50). N est passé comme paramètre lors du chargement du module. Vous pouvez voir comment gérer les paramètres ici :
http://www.tldp.org/LDP/lkmpg/2.6/html/x323.html et ici : https://www.linux.com/tutorials/kernel-newbie-corner-everything-you-wanted-know-about-module-parameters/

Ensuite, à chaque lecture du device node correspondant au module, le driver doit retourner un à la fois ces nombres, en boucle (c.a.d., il va retourner un nombre pour chaque lecture qu’on fait depuis le device, et une fois arrivé au N-ième nombre il doit recommencer du premier) du plus grand au plus petit.

Le code est en annexe (kfifo_ex2.c).

Remarque: Pour cette exercice, je n'avais pas bien lu la consigne. De ce fait, la kfifo se vide du plus petit au plus grand. La correction à cet exercice est faite dans le prochain exercice (Exercice 3)

## Symboles exportés

Un module peut avoir des limitations dans l’accès à des ressources exportées par le noyau ou par d’autres modules selon le type de licence choisi.

Le noyau et les modules peuvent se partager variables, constantes, macros et fonctions. Il faut donc être très attentifs lorsque l’on exporte des symboles, car cela pourrait avoir des effets de bord difficilement prévisibles.

Vous pouvez avoir davantage d’informations concernant les symboles ici : https://www.linux.com/tutorials/kernel-newbie-corner-kernel-symbols-whats-available-your-module-what-isnt/

## Sysfs

Au paravent, une interface permettait aux utilisateurs d’interagir avec le noyau Linux. Le nom de cette interface était procfs, et consistait en un filesystem virtuel avec lequel le noyau pouvait montrer au user-space des informations sur son fonctionnement et il pouvait récupérer des paramètres de configuration. Malheureusement, avec le temps ce filesystem est devenu de plus en plus encombré par des informations non pertinentes, mais la « peur de casser quelque chose » empêchait de faire le ménage. Ainsi, il a été décidé de répartir de zéro, mais d’une façon plus structurée, avec sysfs, tout en gardant procfs.

Les détails de sysfs sont donnés dans la documentation du noyau (fichier Documentation/filesystems/sysfs.txt), et des exemples d’utilisation sont disponibles dans samples/kobject. Une explication très détaillée est donnée dans le chapitre 14 de Linux Device Drivers, 3rd edition. (https://lwn.net/Kernel/LDD3/)

### Exercice 3 sysfs

Le but est de modifier le code de l’exercice précédent pour que le module soit configurable avec sysfs. En particulier, il doit être possible de:

- Changer N pendant l’exécution. Si le nombre qui serait retourné par la prochaine read se situe après le nouveau N, le module doit repartir du premier nombre.
- Afficher la valeur actuelle de N.
- Afficher le numéro d’appels à read effectués jusqu’à présent.
- Afficher le numéro qui sera retourné par le prochain appel à read (sans le sortir de la queue).

Le code est en annexe (kfifo_ex3.c).

Remarque: Pour cette exercice, j'ai pu corriger l'exercice précèdent en vidant la kfifo du plus grand au plus petit.

## Synchronisation

Le noyau Linux est un logiciel multithreadé et multiprocesseur qui est particulièrement sensible aux problèmes de concurrence car il doit, par sa nature, s’adapter aux événements générés par le hardware sous-jacente. Pour cette raison il dispose d’un riche ensemble de primitives de synchronisation, qu’on peut voir dans le chapitre 5 de Linux Device Drivers, 3rd Edition. (https://lwn.net/Kernel/LDD3/)

### Exercice 4 synchronisation (I)

Le but est de reprendre l’exemple pushbutton_example du laboratoire 4 et d'y ajouter :

- Une variable entière shared_var initialisée à 7.
- Un fichier sysfs qui permet de voir la valeur de shared_var.
- Un fichier sysfs add_qty pour incrémenter la valeur de shared_var d’une quantité spécifiée lors de l’écriture dans le fichier.
- Un fichier sysfs decr pour décrémenter la valeur de shared_var de 1.

Qu’est-ce que pourriez-vous utiliser pour gérer les problèmes de concurrence ?

Comme vu en cours, il y a essentiellement 3 méthodes pour gérer les problèmes de concurrence :

- les mutex
- les Spinlock
- Algorithmes sans verrouillage (Ex : RCU)
- les variables atomiques

Le mutex est la principal méthode de verrouillage. Ils permettent de faire une demande pour avoir un accès unique sur la ressource. Si la ressource est déjà utilisée, le processus est bloqué. Il faut donc être dans un contexte où le "sommeil" est autorisé.

Le Spinlock est principalement utilisé lors de contexte où le "sommeil" :
- n'est pas autorisé (Ex: gestionnaires d'interruption)
- n'est pas souhaité (Ex: section critique)

Il est souvent utilisé pour les multiprocesseurs. Contrairement aux mutex, il fait de l'attente active jusqu'à ce que le verrou soit disponible. Il désactive la préemption sur le processeur sur lequel il s'execute.

Si les Spinlock ont de trop fort effet négatif sur la performance du système, il est possible d'utiliser des algorithmes sans verrouillage comme RCU ( Read Copy Update).
API RCU disponible ici : http://en.wikipedia.org/wiki/RCU

Finalement, quand c'est disponible ont peut utiliser les variables atomiques. Elles sont utiliser lorsqu'on manipule des variables entières. Elles permettent d'effectuer des opérations indivisible sur des variables.

Résumé :
- Mutex : Quand le contexte permet le "sommeil"
- Spinlock : Quand le contexte ne permet pas le "sommeil" ou quand le sommeil serait trop couteux (interruptions - sections critiques)
- Algorithme sans verrouillage : Quand le contexte ne permet pas le "sommeil" et que la performance du système est trop impacté par les Spinlocks.
- Variable atomique : Pour protéger des entiers ou des adresses

Pour en savoir plus sur les mécanismes de verrouillage dans le kernel : https://www.kernel.org/doc/html/latest/kernel-hacking/locking.html

Dans le cas de l'exercice 4, nous faisons des accès à la variable entière partagée (shared_var) dans des contextes où le sommeil est permis ( Pas dans une routine de service ni dans une section critique). Dans ce cas, un mutex ferait très bien l'affaire. Étant donné que la ressource a partager est un simple entier, la méthode de variable atomique convient très bien.

Comme demandé, la synchronisation à été basée sur des variables atomiques.

Le code est en annexe (Synch_Ex4.c).

### Exercice 5 synchronisation (II)

Le but est de reprendre le module développé dans l’exercice 4 et de modifier la fonction de gestion des interruptions pour que, à chaque touche d’un bouton, la variable soit incrémentée de 1.

Est-ce que vos alternatives pour la gestion de la concurrence ont changé ? Pourquoi ?

Oui ! Maintenant, nous faisons accès à la ressource dans une routine de service ! Donc, nous ne pouvons pas nous permettre de nous endormir. Il est donc préférable d'utiliser les spinlocks. De plus, ils permettent justement de désactiver les interruptions en même temps à l'aide des fonctions "spin_lock_irqsave" et "spin_unlock_irqrestore".

J'ai donc utiliser les spinlocks comme méthode de synchronisation différente de celle utilisée dans l’exercice 4.

Le code est en annexe (Synch_Ex5.c).

Remarque: Comme dis précédemment, cet exercice à été refait avec une méthode plus correcte afin de ne pas avoir de variables globales. Vous pouvez l'observer dans le fichier "Synch_Ex5_clean.c".

# Conclusion

Je trouve que ce laboratoire a été particulièrement instructif. J'ai l'impression qu'en peu de temps j'ai appris énormément de choses dans tout ce qui concerne les drivers et le codage kernel.
