#include "common.h"

#ifdef __amigaos__
/* Die Spieluhr laeuft ueber die E-Clock, nicht ueber gettimeofday.
 *
 * WARUM: Die libnix-Fassung von gettimeofday holt die Zeit ueber DateStamp,
 * also in Ticks zu 20 ms -- mehr als eine ganze Bildzeit. Das hat nichts
 * Sichtbares heil gelassen, was feiner als 20 ms getaktet ist:
 *   - ZObject::SmoothMove rechnet last_loc + dx*(jetzt-damals). Bei 60 fps
 *     ist `jetzt` in ein bis zwei aufeinanderfolgenden Bildern GLEICH -- die
 *     Einheit steht ein Bild still und springt im naechsten doppelt weit.
 *   - Kartenscrollen mit 400 px/s sah dadurch 8-Pixel-Spruenge.
 *   - Jede Zufallsstreuung unter 20 ms fiel weg. Schussraten streuen in
 *     0,3-ms-Schritten -- auf dem Amiga feuerten damit alle Einheiten eines
 *     Typs im Gleichschritt.
 *   - vcrane.cpp:128 taktet mit 10 ms: der Kranhaken lief halb so schnell.
 *   - ProcessJobs(0.004) konnte sein 4-ms-Budget nicht sehen, die ganze
 *     Wegsuche-Warteschlange fiel deshalb in EIN Bild.
 *
 * Die Zusage der Funktion bleibt unveraendert: Sekunden seit dem ERSTEN
 * Aufruf, als double. Es aendert sich allein die Genauigkeit.
 *
 * Kosten: im Bildpfad liegen 15-16 Abrufe, auf der V1200 also rund 150 us
 * von 19000 -- 0,8 %. Die Aufloesung war das Problem, nicht der Preis.
 *
 * KEINE 64-Bit-Arithmetik: Sekunden und Rest werden getrennt in 32 Bit
 * gefuehrt. Ein `unsigned long long` haette hier `___floatundidf` und eine
 * 64-Bit-Division in den heissesten Pfad des Spiels gezogen. */
#include "fineclock.h"
#endif

namespace COMMON
{

string map_short_name(const string &pfad)
{
	/* Beide Trenner: die Liste kommt aus einer Textdatei, die auf dem PC
	 * geschrieben sein kann. */
	size_t schnitt = pfad.find_last_of("/\\");
	string name = (schnitt == string::npos) ? pfad : pfad.substr(schnitt + 1);

	size_t punkt = name.find_last_of('.');

	/* Nur eine Endung abschneiden, keinen Namen, der mit einem Punkt
	 * beginnt -- sonst bliebe von ".map" nichts uebrig. */
	if(punkt != string::npos && punkt > 0) name = name.substr(0, punkt);

	/* Nie leer zurueckgeben: ein leerer Eintrag waere schlimmer als ein
	 * langer, er waere gar nicht auswaehlbar zu erkennen. */
	return name.empty() ? pfad : name;
}
	
void create_folder(char *foldername)
{
#ifdef WIN32 //if windows
	mkdir(foldername);
#else
    // TODO !
//	mkdir(foldername,-1);
    if( mkdir(foldername, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH) < 0)
    {
        if( errno != EEXIST ){
            std::cerr<<"\n--<error> ["<< __FILE__<<" "<<__LINE__<<"]"
                    <<"\n\t Directories are not created! path: "<<foldername
                   <<"\n\t error info: "<<strerror(errno)<<std::endl;
        }
    }
#endif
}



double current_time()
{
#ifdef WIN32
	//windows version
	static int first_sec = 0;
	static int first_msec = 0;
	SYSTEMTIME st;

	GetSystemTime(&st);

	if(!first_sec)
	{
		first_sec = (int)time(0);
		first_msec = st.wMilliseconds;
	}

	return ((time(0) - first_sec) + ((st.wMilliseconds - first_msec) * 0.001));

#elif defined(__amigaos__)
	static unsigned long letzter  = 0;   /* E-Clock-Stand des letzten Aufrufs */
	static unsigned long sekunden = 0;
	static unsigned long rest     = 0;   /* Ticks, immer kleiner als freq */
	static unsigned long freq     = 0;
	static int laeuft = 0;

	if(!laeuft)
	{
		freq = zod_fineclock_freq();

		/* timer.device noch nicht offen: diesmal ueber gettimeofday, und
		 * beim naechsten Aufruf erneut versuchen. Ohne diesen Rueckweg
		 * stuende die Spieluhr still, falls je etwas vor dem Startcode
		 * die Zeit abfragt. */
		if(!freq)
		{
			struct timeval tv;

			gettimeofday(&tv, nullptr);

			return tv.tv_sec + tv.tv_usec * 0.000001;
		}

		letzter = zod_fineclock_ticks();
		laeuft = 1;
	}

	{
		const unsigned long jetzt = zod_fineclock_ticks();
		/* Vorzeichenlose Differenz -- richtig auch ueber den Ueberlauf des
		 * unteren Langworts hinweg (alle 6050 s), solange zwischen zwei
		 * Aufrufen weniger Zeit liegt. Je Bild ist das erfuellt. */
		unsigned long diff = jetzt - letzter;

		letzter = jetzt;

		sekunden += diff / freq;
		rest     += diff % freq;

		if(rest >= freq) { rest -= freq; sekunden++; }

		return (double)sekunden + (double)rest / (double)freq;
	}

#else
	//linux version
    static long first_sec = 0;
    static long first_usec = 0;
	struct timeval new_time;

	//set current time
    gettimeofday(&new_time, nullptr);

	//set if not set
	if(!first_sec)
	{
		first_sec = new_time.tv_sec;
		first_usec = new_time.tv_usec;
	}

	return (new_time.tv_sec - first_sec) + ((new_time.tv_usec - first_usec) * 0.000001);
#endif
}
	
void split(char *dest, char *message, char split, int *initial, int d_size, int m_size)
{
	int i, a = 0;
	
	for(i = *initial; i < 100100100 && i < m_size; i++)
	{
		// check to see if at end of string
		if (!message[i]) break;
		else if (message[i] != split) //check to see if at the split mark
		{
			if(a < d_size)
			{
				dest[a] = message[i];
				a++;
			}
		}
		else
			break;
	}

	//"cap" return string
	if(a < d_size) dest[a] = 0;
	else if(d_size > 0) dest[d_size - 1] = 0;
	
	//return point to continue search
	if (message[i] == 0)
		*initial = i;
	else
		*initial = ++i;
	
	return;
}

void clean_newline(char *message, int size)
{
	int i;
	
	for(i=0;i<size;i++)
	{
		if(message[i] == '\r')
		{
			message[i] = 0;
			break;
		}
		else if(message[i] == '\n')
		{
			message[i] = 0;
			break;
		}
		else if(!message[i])
			break;
	}
}

void lcase(char *message, int m_size)
{
    // TODO !add locale
    std::locale loc;
    if( message != nullptr ){
        for(int i=0;i<m_size;i++)
            message[i] = static_cast<char>(tolower(message[i],loc));
    }
}

void lcase(string &message)
{
    // TODO !add locale, use c++11
    std::locale loc;
    for(auto &elem : message)
        elem = std::tolower(elem,loc);
//	for(int i=0;i<message.size();i++)
//		message[i] = tolower(message[i]);
}

void uni_pause(int m_sec)
{
#ifdef _WIN32 //if windows
	Sleep(m_sec);	//win version
#else
    usleep(static_cast<uint>(m_sec) * 1000);	//TODO add static_cast<>
#endif
}

void print_dump(char *message, int size, char *name)
{
	int i;
	
	ZLOG("raw dump:%s:", name);
	for(i=0;i<size;i++)
        ZLOG("%2.2x ", message[i]);
        // ZLOG("%0.2x ", message[i]); //TODO! '0' flag ignored with precision and ‘%x’ gnu_printf format
	ZLOG("\n");
}

bool points_within_distance(int x1, int y1, int x2, int y2, int distance)
{
	//quick prelim tests
	if(x2 < x1 - distance) return false;
	if(x2 > x1 + distance) return false;
	if(y2 < y1 - distance) return false;
	if(y2 > y1 + distance) return false;

	//semi quick tests
    // TODO static_cast & round
    int sh_dist = static_cast<int>(std::floor(distance * 0.707106781 + 0.5)); //sin(45)
	int dx = abs(x1 - x2);
	int dy = abs(y1 - y2);
	if(dx < sh_dist && dy < sh_dist) return true;

    //slow test
    if(sqrt((dx * dx) + (dy * dy)) > distance) // TODO delete (float)
        return false;


//    static_cast<int>
	return true;
}

bool points_within_area(int px, int py, int ax, int ay, int aw, int ah)
{
	if(px < ax) return false;
	if(py < ay) return false;
	if(px > ax + aw) return false;
	if(py > ay + ah) return false;

	return true;
}

bool good_user_char(int c)
{
	if(isalnum(c)) return true;
	if(c == ' ') return true;
	if(c == '@') return true;
	if(c == '.') return true;
	if(c == '_') return true;
	if(c == '-') return true;

	return false;
}

bool good_user_string(const char *message)
{
	int i, len;

    len = static_cast<int>(strlen(message));

	if(!strlen(message)) return false;

	for(i=0;message[i];i++)
		if(!good_user_char(message[i]))
			return false;

	//any double spaces?
	for(i=0;i<len-1;i++)
		if(message[i] == ' ' && message[i+1] == ' ')
			return false;

	//spaces at ends?
	if(message[0] == ' ') return false;
	if(message[len-1] == ' ') return false;

	return true;
}

void printd_reg(char *message)
{
	FILE *ofp;
	struct tm *ptr;
	time_t lt;
	char timebuf[100];

    // TODO nullptr
    lt = time(nullptr);
	ptr = localtime(&lt);
	
	ofp = fopen("reg_log.txt","a");

	strcpy(timebuf, asctime(ptr));
	clean_newline(timebuf, 100);

	fprintf(ofp, "%-12s :: %s\n", timebuf, message);
		
	fclose(ofp);
}

string data_to_hex_string(unsigned char *data, int size)
{
	int i;
	char buf[50];
	string output;

	for(i=0; i<size; i++)
	{
		snprintf(buf, sizeof(buf), "%02x", data[i]);

		output += buf;
	}

	return output;
}

bool file_can_be_written(char *filename)
{
	FILE *fp;

	fp = fopen(filename, "a");

	if(!fp) return false;

	fclose(fp);

	return true;
}

vector<string> directory_filelist(string foldername)
{
	vector<string> filelist;

#ifdef _WIN32

	HANDLE hFind = INVALID_HANDLE_VALUE;
	WIN32_FIND_DATA ffd;

	foldername += "*";

	hFind = FindFirstFile(foldername.c_str(), &ffd);

	if(INVALID_HANDLE_VALUE == hFind) return filelist;

	do
	{
		if(!(ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
			filelist.push_back((char*)ffd.cFileName);
	} 
	while (FindNextFile(hFind, &ffd) != 0);

	FindClose(hFind);

#else
	DIR *dp;
    struct dirent *dirp;

	if(!foldername.size()) foldername = ".";

	dp  = opendir(foldername.c_str());

	if(!dp) return filelist;
// TODO nullptr
    while ((dirp = readdir(dp)) != nullptr)
	{
		if(dirp->d_type == DT_REG) 
			filelist.push_back(dirp->d_name);
	}

	closedir(dp);

#endif

	//for(int i=0;i<filelist.size(); i++) ZLOG("filelist found:%s\n", filelist[i].c_str());

	return filelist;
}

void parse_filelist(vector<string> &filelist, string extension)
{
	lcase(extension);

	for(vector<string>::iterator i=filelist.begin(); i!=filelist.end();)
	{
		string cur_filename;
        // TODO c++11 use
        //int pos;
        //int good_pos;

		cur_filename = *i;

		lcase(cur_filename);

        auto pos = cur_filename.rfind(extension);
        auto good_pos = cur_filename.size() - extension.size();

		if(pos == string::npos || pos != good_pos)
			i = filelist.erase(i);
		else
			++i;
	}
}

bool sort_string_func (const string &a, const string &b)
{
	return strcmp(a.c_str(), b.c_str()) < 0;
}

};


