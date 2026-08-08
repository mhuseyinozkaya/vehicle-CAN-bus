Ford araçlarda High Speed **HS-CAN** ve Medium Speed **MS-CAN** olmak üzere iki tane ayrı CAN ağı vardır.

HS-CAN ağında, aracın kritik işlemleri yürütülür. Motorun çalışması, devri, şanzıman kontrolü vb.

MS-CAN ağı ise daha çok sistemin geri kalanının haberleştiği ağdır. Örneğin klimaların açılıp kapanması, kapı, radyo, araç kapıları vb. işlemler bu ağda haberleşir.

- HS-CAN hattı 500KBps için OBDII pin numaraları H(pin 6)/L(pin 14) 
- MS-CAN hattı 125KBps için OBDII pin numaraları H(pin 3)/L(pin 11)

Not: CAN ağları fiziksel olarak ayrı olduğu için hem HS-CAN hem de MS-CAN ağında aynı CAN ID'ye sahip paketler olabilir. 
