QuizzGame (B) [Propunere Continental]

Implementati un server multithreading care suporta oricati clienti. 
Serverul va coordona clientii care raspund la un set de intrebari pe rand, in ordinea in care s-au inregistrat. 
Fiecarui client i se pune o intrebare si are un numar n de secunde pentru a raspunde la intrebare. 
Serverul verifica raspunsul dat de client si daca este corect va retine punctajul pentru acel client. 
De asemenea, serverul sincronizeaza toti clienti intre ei si ofera fiecaruia un timp de n secunde pentru a raspunde. 
Comunicarea intre server si client se va realiza folosind socket-uri. 
Toata logica va fi realizata in server, clientul doar raspunde la intrebari. 
Intrebarile cu variantele de raspuns vor fi stocate fie in fisiere XML fie intr-o baza de date SQLite. 
Serverul va gestiona situatiile in care unul din participanti paraseste jocul astfel incat jocul sa continue fara probleme. 
