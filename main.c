

#include "ltr/include/ltr24api.h"
#include <liquid/liquid.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ncurses.h>
#include <termios.h>
#include <locale.h>
#include <time.h>
#include <sys/types.h>
#include <dirent.h>
#include <ctype.h>
#include "version.h"
#ifdef _WIN32
#include <locale.h>
#include <conio.h>
#else
#include <signal.h>
#include <unistd.h>
#endif


/* количество отсчетов на канал, принмаемых за раз */
#define RECV_BLOCK_CH_SIZE  4096
/* таймаут на ожидание данных при приеме */
#define RECV_TOUT  4000

float paramF1 = 94;             // частота 1
float paramF2 = 96;             // частота 2
float paramBaudRate = 0.5;      // символьная скорость
float paramFreqADC = 00000.0;   // частота дискретезации
float paramPorog = 0.01;        // порог в вольтах
float SNR = 12.0f;              // С/Ш в дБ
float tmean = 10;               // время усреднения шумов
char fname[256] = "tst24";      // имя файла
int numch = 4;


typedef struct {
    int slot;
    const char *serial;
    DWORD addr;
    unsigned char freq_rate;
} t_open_param;


/* признак необходимости завершить сбор данных */
static int f_out = 0;

#ifndef _WIN32
/* Обработчик сигнала завершения для Linux */
static void f_abort_handler(int sig)
{
    f_out = 1;
}
#endif

WINDOW *term,*stat,*wdebug;

/*------------------------------------------------------------------------------------------------*/
static int f_get_params(int argc, char **argv, t_open_param *par) {
    /* Разбор параметров командной строки. Если указано меньше, то используются
     * значения по умолчанию:
     * 1 параметр - номер слота (от 1 до 16)
     * 2 параметр - номер частоты дискретизации
     * 3 параметр - серийный номер крейта
     * 4 параметр - ip-адрес сервера
     */
    int err = 0;

    par->slot = LTR_CC_CHNUM_MODULE1;
    par->freq_rate = LTR24_FREQ_14K;
    par->serial = "";
    par->addr = LTRD_ADDR_DEFAULT;


    if (argc > 1)
        par->slot = atoi(argv[1]);
    if (argc > 2)
        par->freq_rate = atoi(argv[2]);
    if (argc > 3)
        par->serial = argv[3];
    if (argc > 4) {
        int a[4];
        int i;
        if (sscanf(argv[4], "%d.%d.%d.%d", &a[0], &a[1], &a[2], &a[3]) != 4) {
            wprintw(wdebug, "Неверный формат IP-адреса!!\n");wrefresh(wdebug);
            err = -1;
        }

        for (i = 0; ((i < 4) && !err); i++) {
            if ((a[i] < 0) || (255 < a[i])) {
                wprintw(wdebug, "Недействительный IP-адрес!!\n");wrefresh(wdebug);
                err = -1;
            }
        }

        if (!err)
            par->addr = (a[0] << 24) | (a[1] << 16) | (a[2] << 8) | a[3];
    }

    return err;
}



void init_windows(void){
  initscr();
  start_color();
  init_pair(1,COLOR_WHITE,COLOR_BLUE);
  init_pair(2,COLOR_GREEN,COLOR_BLACK);
  init_pair(3,COLOR_MAGENTA,COLOR_BLUE);
  bkgd(COLOR_PAIR(3));
  noecho();
  halfdelay(100);
  term=subwin(stdscr,15,118,13,1);
  stat=subwin(stdscr,10,118,28,1);
  wdebug=subwin(stdscr,12,118,1,1);
  wbkgd(term,COLOR_PAIR(2));
  wbkgd(stat,COLOR_PAIR(1));
  wbkgd(wdebug,COLOR_PAIR(1));
  scrollok(term,true);
  scrollok(wdebug,true);
  move(0,1);
  curs_set(0);
  attron(A_BOLD);
  printw("SSDR_Terminal %s (%s.%s.%s)", FULLVERSION_STRING, DATE, MONTH, YEAR);

  refresh();

}

void param_setting(void)
{
    werase(term);wrefresh(term);
    mvwprintw(term,1,1,"Настройка параметров");wrefresh(term);
    echo();
    curs_set(1);

      mvwprintw(term,2,1,"F1:");wrefresh(term);
      mvwscanw(term,2,5,"%f\r",&paramF1);wrefresh(term);

      mvwprintw(term,3,1,"F2:");wrefresh(term);
      mvwscanw(term,3,5,"%f\r",&paramF2);wrefresh(term);

      mvwprintw(term,4,1,"BR:");wrefresh(term);
      mvwscanw(term,4,5,"%f\r",&paramBaudRate);wrefresh(term);

      mvwprintw(term,5,1,"SNR(дБ):");wrefresh(term);
      mvwscanw(term,5,10,"%f\r",&SNR);wrefresh(term);

      mvwprintw(term,6,1,"Время вычисления порога (сек):");wrefresh(term);
      mvwscanw(term,6,32,"%f\r",&tmean);wrefresh(term);

      mvwprintw(term,7,1,"Имя файла:");wrefresh(term);
      mvwscanw(term,7,12,"%s\r",&fname);wrefresh(term);

      mvwprintw(term,8,1,"Число каналов (def = 4):");wrefresh(term);
      mvwscanw(term,8,25,"%f\r",&numch);wrefresh(term);


    noecho();
    curs_set(0);
    werase(term);wrefresh(term);
 }
long mtime()
{
  struct timespec t;

  clock_gettime(CLOCK_REALTIME, &t);
  long mt = (long)t.tv_sec * 1000 + t.tv_nsec / 1000000;
  return mt;
}

int main(int argc, char** argv)
{
    INT err = LTR_OK;
    TLTR24 hltr24;
    t_open_param par;

    setlocale( 0, "" );
    init_windows();
//    halfdelay(1);

    int key=1, rowcnt = 0;



#ifndef _WIN32
    struct sigaction sa;
    /* В ОС Linux устанавливаем свой обработчик на сигнал закрытия,
       чтобы завершить сбор корректно */
    sa.sa_handler = f_abort_handler;
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
#endif

#ifdef _WIN32
    /* для вывода русских букв в консоль для ОС Windows*/
    setlocale(LC_ALL, "");
#endif


    err = f_get_params(argc, argv, &par);

    if (!err)
    {
        LTR24_Init(&hltr24);
        /* Устанавливаем соединение с модулем */
        err = LTR24_Open(&hltr24, par.addr, LTRD_PORT_DEFAULT, par.serial, par.slot);
        if (err!=LTR_OK)
        {
            wprintw(wdebug, "Не удалось установить связь с модулем. Ошибка %d (%s)\n",
                    err, LTR24_GetErrorString(err));wrefresh(wdebug);
        }
        else
        {
            INT ch_cnt=0;
            INT close_err;

            /* Читаем информацию о модуле, включая калибровки, из Flash-памяти
             * модуля. Без вызова этой функции нельзя будет получить правильные
             * откалиброванные значения */
            err = LTR24_GetConfig(&hltr24);
            if (err!=LTR_OK)
            {
                wprintw(wdebug, "Не удалось прочитать информацию о модуле из Flash-памяти. Ошибка %d (%s)\n",
                        err, LTR24_GetErrorString(err));wrefresh(wdebug);
            }
            else
            {
                /* Выводим прочитанную информацию о модуле */
                wprintw(wdebug, "Модуль открыт успешно! Информация о модуле: \n");
                wprintw(wdebug, "Название модуля    = %s\n", hltr24.ModuleInfo.Name);
                wprintw(wdebug, "Серийный номер     = %s\n", hltr24.ModuleInfo.Serial);
                wrefresh(wdebug);
            }

                wprintw(wdebug,"Для запуска приемника нажмите 's'.\n");
                mvwprintw(stat,1,0,"Прием данных не запущен.");wrefresh(stat);


            do
            {
                wprintw(wdebug,"Для настройки параметров нажмите 'p'.\n");
                wprintw(wdebug,"Для выхода нажмите 'q'.\n");
                wrefresh(wdebug);

                halfdelay(1000);
                key = getch();
            while(key!=0)
            {
            if (key == 'p') {param_setting(); key = 's';break;}
            else if (key == 'q') break;
            else if (key == 's') break;
            else key = getch();
            }
//                key = getch();

            if (key == 's')
            {

                halfdelay(1);
            /* Настройка модуля */
            if (err==LTR_OK)
            {
                /* Формат - 24 или 20 битный */
                hltr24.DataFmt = LTR24_FORMAT_24;
                /* Устанавливаем частоту с помощью одной из констант (Для 24-битного режима
                   макс. частота только при 2-х каналах, все 4 - только пр 58)  */
                hltr24.ADCFreqCode = par.freq_rate;
                /* Вкл./откл. тестовых режимов (измерение нуля/ICP-тест) */
                hltr24.TestMode = FALSE;

                /* Настройка режимов каналов */
                int cnt;
                for(cnt = 0; cnt>numch; cnt++)
                {
                    hltr24.ChannelMode[cnt].Enable = TRUE;
                    hltr24.ChannelMode[cnt].Range = LTR24_RANGE_2;
                    hltr24.ChannelMode[cnt].AC = TRUE;
                    hltr24.ChannelMode[cnt].ICPMode = FALSE;
                }

                err = LTR24_SetADC(&hltr24);
                if (err!=LTR_OK)
                {
                    wprintw(wdebug, "Не удалось установить настройки АЦП: Ошибка %d (%s)\n",
                            err, LTR24_GetErrorString(err));wrefresh(wdebug);
                }
                else
                {
                    INT i;
                    /* подсчитываем кол-во разрешенных каналов */
                    for (i = 0, ch_cnt=0; i < LTR24_CHANNEL_NUM; i++)
                    {
                        if (hltr24.ChannelMode[i].Enable)
                            ch_cnt++;
                    }

                    /* после SetADC() обновляется поле AdcFreq. Становится равной действительной
                     * установленной частоте */

                    paramFreqADC = hltr24.ADCFreq;

                    wprintw(wdebug,"Настройки АЦП установленны успешно. Частота = %.2f Гц, Кол-во каналов = %d\n",
                            hltr24.ADCFreq, ch_cnt);
                    wrefresh(wdebug);
                }
            }

                strcat(fname,"_");
                char sn[4], tmpname[256];
                int num = -1, nbuff = 0, nl = strlen(fname);

                DIR * dir = opendir("data/");
                struct dirent *entry;
                if (dir!= NULL) {
                    while ( (entry = readdir(dir)) != NULL) {
                        strcpy(tmpname,entry->d_name);
                        if((strspn(fname,tmpname)==nl) && isdigit(tmpname[nl]) && isdigit(tmpname[nl+1])
                                                       && isdigit(tmpname[nl+2]) && isdigit(tmpname[nl+3])) {
                            nbuff = atoi(strncpy(sn,tmpname+nl,4));
                            if( nbuff>= num ) num = nbuff;
                        }
                    }
                }
                closedir(dir);


                FILE * fid = 0;

                char boud[] = {'.','E',0x0D,'A',' ','S','I','U',0x0A,'D','R','J','N','F','C','K','T','Z','L','W','H','Y','P','Q','O','B','G',' ','M','X','V',';'};
                char aboud[] ={';','V','X','M',' ','G','B','O','Q','P','Y','H','W','L','Z','T','K','C','F','N','J','R','D',0x0A,'U','I','S',' ','A',0x0D,'E','.'};

                int   order = 6;                                      // filter order
                float f01 = paramF1/paramFreqADC;                     // center frequency f1
                float f02 = paramF2/paramFreqADC;                     // center frequency f2
                float fc1 = f01+paramBaudRate/paramFreqADC;           // cutoff frequency f1
                float fc2 = f02+paramBaudRate/paramFreqADC;           // cutoff frequency f2
                float fcn = 4*paramBaudRate/paramFreqADC;             // cutoff frequency fn
                float Ap = 1.0f;                                      // pass-band ripple
                float As = 60.0f;                                     // stop-band attenuation
                float samplenum = round(paramFreqADC/paramBaudRate);  // число выборок на один бит


                int ind = 0,i,j,cur_pos;                                    // индексы циклов

                float tdelay = 0;                                   // время установления фильтров
                int tsave = 300;                                    // максимальное время записи

                if(1.5/paramBaudRate>6.25)
                    tdelay = 1.5/paramBaudRate;
                else
                    tdelay = 6.25;


            // сигналы после первой фильтрации
            // Канал 1
                float s1f1, s1f2, s2f1, s2f2, s3f1, s3f2, s4f1, s4f2;         // исходные сигналы с каждого канала
                float s1, s2, s3, s4;
                float y1, y2, y3, y4;


                float y0, noiselevel = 0, nporog, k=0, sigma = 0;
                long t1;

                unsigned charcode = 0;
                unsigned bitnum = 1;
                unsigned detect = 0;

                int strd = 0, charcnt = 0, psflag = 0, og = 0;

            // Параметры фильтров
                liquid_iirdes_filtertype ftype  = LIQUID_IIRDES_BESSEL;     //CHEBY2;
                liquid_iirdes_bandtype   btype  = LIQUID_IIRDES_BANDPASS;
                liquid_iirdes_bandtype   btypel = LIQUID_IIRDES_LOWPASS;
                liquid_iirdes_format     format = LIQUID_IIRDES_SOS;


            // фильтры первого канала
                iirfilt_rrrf c1f1 = iirfilt_rrrf_create_prototype(ftype, btype, format, order, fc1, f01, Ap, As);
                iirfilt_rrrf c1f2 = iirfilt_rrrf_create_prototype(ftype, btype, format, order, fc2, f02, Ap, As);

            // фильтры второго канала
                iirfilt_rrrf c2f1 = iirfilt_rrrf_create_prototype(ftype, btype, format, order, fc1, f01, Ap, As);
                iirfilt_rrrf c2f2 = iirfilt_rrrf_create_prototype(ftype, btype, format, order, fc2, f02, Ap, As);

            // фильтры третьего канала
                iirfilt_rrrf c3f1 = iirfilt_rrrf_create_prototype(ftype, btype, format, order, fc1, f01, Ap, As);
                iirfilt_rrrf c3f2 = iirfilt_rrrf_create_prototype(ftype, btype, format, order, fc2, f02, Ap, As);

            // фильтры четвертого канала
                iirfilt_rrrf c4f1 = iirfilt_rrrf_create_prototype(ftype, btype, format, order, fc1, f01, Ap, As);
                iirfilt_rrrf c4f2 = iirfilt_rrrf_create_prototype(ftype, btype, format, order, fc2, f02, Ap, As);

            // финальный ФНЧ
                iirfilt_rrrf lp1 = iirfilt_rrrf_create_prototype(ftype, btypel, format, order, fcn,   0, Ap, As);
                iirfilt_rrrf lp2 = iirfilt_rrrf_create_prototype(ftype, btypel, format, order, fcn,   0, Ap, As);
                iirfilt_rrrf lp3 = iirfilt_rrrf_create_prototype(ftype, btypel, format, order, fcn,   0, Ap, As);
                iirfilt_rrrf lp4 = iirfilt_rrrf_create_prototype(ftype, btypel, format, order, fcn,   0, Ap, As);


            // вывод параметров приемника
                werase(stat);
                mvwprintw(stat,1,90,"samplenum  =   %12.0f",samplenum);
                mvwprintw(stat,2,90,"freqADC    =     %10.2f",paramFreqADC);
                mvwprintw(stat,3,90,"F1         =     %10.2f",paramF1);
                mvwprintw(stat,4,90,"F2         =     %10.2f",paramF2);
                mvwprintw(stat,5,90,"BR         =        %7.5f",paramBaudRate);
                wrefresh(stat);



            if (err==LTR_OK)
            {
                DWORD recvd_blocks=0;
                INT recv_data_cnt = RECV_BLOCK_CH_SIZE*ch_cnt;
                /* В 24-битном формате каждому отсчету соответствует два слова от модуля,
                   а в 20-битном - одно */
                INT   recv_wrd_cnt = recv_data_cnt*(hltr24.DataFmt==LTR24_FORMAT_24 ? 2 : 1);
                DWORD  *rbuf = (DWORD*)malloc(recv_wrd_cnt*sizeof(rbuf[0]));
                double *data = (double *)malloc(recv_data_cnt*sizeof(data[0]));
                BOOL   *ovlds = (BOOL *)malloc(recv_data_cnt*sizeof(ovlds[0]));

                if ((rbuf==NULL) || (data==NULL) || (ovlds==NULL))
                {
                    wprintw(wdebug, "Ошибка выделения памяти!\n");wrefresh(wdebug);
                    err = LTR_ERROR_MEMORY_ALLOC;
                }

                if (err==LTR_OK)
                {
                    /* Запуск сбора данных */
                    err=LTR24_Start(&hltr24);
                    if (err!=LTR_OK)
                    {
                        wprintw(wdebug, "Не удалось запустить сбор данных! Ошибка %d (%s)\n",
                                err, LTR24_GetErrorString(err));wrefresh(wdebug);
                    }
                }

                if (err==LTR_OK)
                {
                    wprintw(wdebug,"Сбор данных запущен. Для останова нажмите 'q'\n");
                    wprintw(wdebug,"Определение шумового порога за %.0f сек\n",tmean);
                    wrefresh(wdebug);
                }

                /* ведем сбор данных до возникновения ошибки или до
                 * запроса на завершение */
                while (!f_out && (err==LTR_OK))
                {
                    INT recvd;
                    /* В таймауте учитываем время сбора запрашиваемого числа отсчетов */
                    DWORD tout = RECV_TOUT + (DWORD)(1000.*RECV_BLOCK_CH_SIZE/hltr24.ADCFreq + 1);

                    /* Прием данных от модуля.  */
                    recvd = LTR24_Recv(&hltr24, rbuf, NULL, recv_wrd_cnt, tout);

                    /* Значение меньше нуля соответствуют коду ошибки */
                    if (recvd<0)
                    {
                        err = recvd;
                        wprintw(wdebug, "Ошибка приема данных. Ошибка %d:%s\n",
                                err, LTR24_GetErrorString(err));wrefresh(wdebug);
                    }
                    else if (recvd!=recv_wrd_cnt)
                    {
                        wprintw(wdebug, "Принято недостаточно данных. Запрашивали %d, приняли %d\n",
                                recv_wrd_cnt*ch_cnt, recvd);wrefresh(wdebug);
                        err = LTR_ERROR_RECV_INSUFFICIENT_DATA;
                    }
                    else
                    {
                        err = LTR24_ProcessData(&hltr24, rbuf, data, &recvd,
                                                LTR24_PROC_FLAG_VOLT |
                                                LTR24_PROC_FLAG_CALIBR |
                                                LTR24_PROC_FLAG_AFC_COR,
                                                ovlds);
                        if (err!=LTR_OK)
                        {
                            wprintw(wdebug, "Ошибка обработки данных. Ошибка %d:%s\n",
                                    err, LTR24_GetErrorString(err));wrefresh(wdebug);
                        }
                        else
                        {

                            i = 0;
                            recvd_blocks++;
                            mvwprintw(stat,0,90,"Принято блоков:            ");
                            mvwprintw(stat,0,90,"Принято блоков: %d",recvd_blocks);wrefresh(stat);

                            if(strd == 0)
                            {
                                if(k<round(paramFreqADC*tdelay))
                                    {mvwprintw(stat,6,90,"Установление фильтров %4.1f",tdelay-k/paramFreqADC);wrefresh(stat);}
                                else
                                    {mvwprintw(stat,6,90,"Вычисление порога %4.1f    ",tmean+tdelay-k/paramFreqADC);wrefresh(stat);}
                            }


                            /* выводим по первому слову на канал */
                            /* производится фильтраця каналов */
                            t1 = mtime();
                            for (cur_pos=0; cur_pos < LTR24_CHANNEL_NUM*RECV_BLOCK_CH_SIZE; cur_pos+=numch)
                            {

                                iirfilt_rrrf_execute(c1f1, data[cur_pos], &s1f1);        // канал 1 f1
                                iirfilt_rrrf_execute(c1f2, data[cur_pos], &s1f2);        // канал 1 f2

                                iirfilt_rrrf_execute(c2f1, data[cur_pos+1], &s2f1);      // канал 2 f1
                                iirfilt_rrrf_execute(c2f2, data[cur_pos+1], &s2f2);      // канал 2 f2

                                iirfilt_rrrf_execute(c3f1, data[cur_pos+2], &s3f1);      // канал 3 f1
                                iirfilt_rrrf_execute(c3f2, data[cur_pos+2], &s3f2);      // канал 3 f2

                                iirfilt_rrrf_execute(c4f1, data[cur_pos+3], &s4f1);      // канал 4 f1
                                iirfilt_rrrf_execute(c4f2, data[cur_pos+3], &s4f2);      // канал 4 f2

                                if(numch == 1)
                                {
                                    s2f1 = 0;   s2f2 = 0;
                                    s3f1 = 0;   s3f2 = 0;
                                    s4f1 = 0;   s4f2 = 0;
                                }
                                else if(numch == 2)
                                {
                                    s3f1 = 0;   s3f2 = 0;
                                    s4f1 = 0;   s4f2 = 0;
                                }
                                else if(numch ==3 )
                                {
                                    s4f1 = 0;   s4f2 = 0;
                                }

                                s1 = pow(s1f1,2)-pow(s1f2,2);
                                s2 = pow(s2f1,2)-pow(s2f2,2);
                                s3 = pow(s3f1,2)-pow(s3f2,2);
                                s4 = pow(s4f1,2)-pow(s4f2,2);

                                iirfilt_rrrf_execute(lp1, s1, &y1);
                                iirfilt_rrrf_execute(lp2, s2, &y2);
                                iirfilt_rrrf_execute(lp3, s3, &y3);
                                iirfilt_rrrf_execute(lp4, s4, &y4);

                                y0 = y1+y2+y3+y4;
                                i++;

                                //detecting
                                if(strd)    // start detecting
                                {
                                    if(!detect)
                                    {
                                        if ((y0>=paramPorog)||(y0<=-paramPorog))
                                        {
                                            j=0;
                                            detect=1;
                                            mvwprintw(stat,0,0,"SIGNAL");
                                            wrefresh(stat);
                                        }
                                    }
                                    else
                                    {
                                        if(j==floor(samplenum/2))
                                        {
                                            if(y0>=paramPorog)
                                            {
                                                charcode+=bitnum;
                                                bitnum=bitnum<<1;
                                                og = 1;
                                            }
                                            if(y0<-paramPorog)
                                            {
                                                bitnum=bitnum<<1;
                                                og = -1;
                                            }
                                            if((y0<paramPorog)&&(y0>-paramPorog))
                                            {
                                                detect=0;
                                                j=0;
                                                bitnum=1;
                                                charcode=0;
                                                charcnt = 0;
                                                mvwprintw(stat,0,0,"      ");wrefresh(stat);
                                                og = 0;
                                             }
                                             mvwprintw(stat,2,0,"Принято бит: %0.0f",log2f(bitnum));
                                        }
                                        if(j==samplenum){j=0;}

                                        if(bitnum==32)
                                        {
                                            if(charcnt==0) rowcnt++;

                                            mvwprintw(term,rowcnt,charcnt+1,"%c",boud[charcode]);
                                            mvwprintw(term,rowcnt,charcnt+59,"%c",aboud[charcode]);
                                            wrefresh(term);

                                            bitnum=1;
                                            charcode=0;
                                            charcnt++;
                                        }
                                    }
                                    j++;


                                }
                                else
                                {
                                    if(k>round(paramFreqADC*tdelay))
                                        noiselevel+=pow(y0,2);

                                    if(k==round(paramFreqADC*(tmean+tdelay)))
                                    {
                                        strd = 1; // флаг начала детектирования
                                        sigma = sqrt(noiselevel/k);
                                        nporog = pow(10,SNR/20.0f)*3*sigma;
                                        mvwprintw(stat,6,90,"nporog(y0) =   %.4e В",nporog);
                                        mvwprintw(stat,7,90,"σ(y0)      =   %.4e В",sigma);
                                        wrefresh(stat);
                                        paramPorog = nporog;
                                        wprintw(wdebug,"Прием начался.\n");wrefresh(wdebug);
                                        mvwprintw(stat,2,0,"Принято бит: %0.0f",log2f(bitnum));
                                        mvwprintw(stat,1,0,"Идет детектирование сообщения.");wrefresh(stat);
                                    }
                                    else k++;
                                }

//                                     saving file

                                    if ((ind<=paramFreqADC*tsave)&&(psflag==1))
                                    {
                                        fprintf(fid,"%.4e %.4e %.4e %.4e %.4e %d\n",data[cur_pos], data[cur_pos+1], data[cur_pos+2], data[cur_pos+3], y0, og);
                                        ind++;
                                        if(ind>paramFreqADC*tsave)
                                            {fclose(fid); ind = 0;psflag=0;}

                                    }

                            }
                            t1 = mtime()-t1;
                            mvwprintw(stat,3,0,"Время выполнения основного цикла: %ld мс   ",t1);wrefresh(stat);
                        }
                    }
                    key = getch();
                    if(key == 'q'){mvwprintw(stat,1,0,"Прием данных остановлен.     ");wrefresh(stat);break;}
                    if(key == 'd'){if(psflag == 0) {
                                   psflag = 1;
                                   num++;
                                   char OUTPUT_FILENAME[256];
                                   sprintf(OUTPUT_FILENAME,"data/%s%d%d%d%d.dat",fname,num/1000,num/100,num/10,num);
                                   fid = fopen(OUTPUT_FILENAME,"w");
                                   wprintw(wdebug,"Запись в файл %s\n",OUTPUT_FILENAME);wrefresh(wdebug);}}
                    if(key == 'f'){if(psflag== 1) {
                                   psflag = 0;
                                   fclose(fid);
                                   wprintw(wdebug,"Останов записи в файл\n");wrefresh(wdebug);}}
                    if(key == 'c'){werase(term);wrefresh(term);rowcnt = 0;}


#ifdef _WIN32
                    /* проверка нажатия клавиши для выхода */
                    if (err==LTR_OK)
                    {
                        if (_kbhit())
                            f_out = 1;
                    }
#endif
                } //while (!f_out && (err==LTR_OK))

                    if(psflag==1) fclose(fid);

                    iirfilt_rrrf_destroy(c1f1);
                    iirfilt_rrrf_destroy(c1f2);

                    iirfilt_rrrf_destroy(c2f1);
                    iirfilt_rrrf_destroy(c2f2);

                    iirfilt_rrrf_destroy(c3f1);
                    iirfilt_rrrf_destroy(c3f2);

                    iirfilt_rrrf_destroy(c4f1);
                    iirfilt_rrrf_destroy(c4f2);

                    iirfilt_rrrf_destroy(lp1);
                    iirfilt_rrrf_destroy(lp2);
                    iirfilt_rrrf_destroy(lp3);
                    iirfilt_rrrf_destroy(lp4);
                /* по завершению останавливаем сбор, если был запущен */
                if (hltr24.Run)
                {

                    INT stop_err = LTR24_Stop(&hltr24);
                    if (stop_err!=LTR_OK)
                    {
                        wprintw(wdebug, "Не удалось остановить сбор данных. Ошибка %d:%s\n",
                                stop_err, LTR24_GetErrorString(stop_err));wrefresh(wdebug);
                        if (err==LTR_OK)
                            err = stop_err;
                    }
                    else
                    {
                        wprintw(wdebug,"Сбор остановлен успешно.\n");
                        wrefresh(wdebug);
                    }
                }
            }
            }
            wprintw(wdebug,"Для запуска приемника нажмите 's'.\nНажмите 'e' для отключения модуля\n");wrefresh(wdebug);
            halfdelay(1000);
            key=getch();
            while(key!=0)
            {
            if(key == 'e')     break;
            else if (key == 's') break;
            else                key=getch();
            }

            } while(key == 's');
            /* закрываем связь с модулем */
            close_err = LTR24_Close(&hltr24);
            if (close_err!=LTR_OK)
            {
                wprintw(wdebug, "Не удалось закрыть связь с модулем. Ошибка %d:%s\n",
                        close_err, LTR24_GetErrorString(close_err));wrefresh(wdebug);
                if (err==LTR_OK)
                    err = close_err;
            }
            else
            {
                wprintw(wdebug,"Связь с модулем успешно закрыта.\n");
                wrefresh(wdebug);
            }
        }

    }
    wprintw(wdebug,"Для завершения работы нажмите любую клавишу...\n");
    wrefresh(wdebug);
    while(key!=0) { getch(); break;}
    delwin(term);
    delwin(stat);
    delwin(wdebug);
    endwin();
    return err;
}
