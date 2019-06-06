#include <iostream>
#include <fstream>
#include <windows.h>
#include <string>
#include <cmath>
#include <fstream>
#include <intrin.h>
#include "Timer.h"

using namespace std;


//Parametry testow
//      MEMSIZE
const unsigned long long MIN_SIZE = 1024; //W bajtach
const unsigned long long MAX_SIZE = 1024 * 1024 * 128; //32MiB
//const unsigned long long MAX_SIZE = 1024 * 1024 * 16;
//const unsigned long long MAX_SIZE = pow(2, 29);
const long long SIZE_VOID = sizeof(void*);
const unsigned long long SIZE_CHAR = sizeof(char*);

//Ilosc testow przy random_chase_bechmark i linear_chain_benchmark
const int BENCH_NUM = 100;

//      STRIDE
const unsigned long long MIN_STRIDE_SIZE = SIZE_VOID;
const unsigned long long MAX_STRIDE_SIZE = 1024;

//      CACHE_SIZE = L1 + L2 + L3
const unsigned long long CACHE_SIZE = ((4 * 32 * 1024) + (4 * 256 * 1024) + (6 * 1024 * 1024)) * 10;
const unsigned long long LINE_SIZE = 64;


volatile void* chase_pointers_global;


//      DISTANCES
const unsigned long long ROW = pow(2, 10);
const unsigned long long BANK = pow(2, 25);
const unsigned long long BANK_GROUP = pow(2, 28);

unsigned long long distances[5]{ pow(2,5), pow(2,7), ROW, BANK, BANK_GROUP };




// Zwraca pointer chaser o zadanym rozmiarze ktory odwiedza wszystkie lokalizacje pamieci w losowej kolejnosci
void** create_random_chain(unsigned long long size)
{
	int min_diff = LINE_SIZE / SIZE_VOID + 1;
	long long len = size / SIZE_VOID;
	void** buffer = new void* [len];



	int* indices = new int[len];
	for (int i = 0; i < len; ++i)
		indices[i] = i;
	bool* visited = new bool[len];
	for (int i = 0; i < len; i++)
		visited[i] = false;
	visited[0] = true;
	bool chg;

	//Shuffle indeksow z zachowaniem min_diff (Rozmiar linii / rozmiar (void*))
	for (int i = 1; i < len; i++)
	{
		chg = false;
		while (abs(indices[i] - indices[i - 1]) < min_diff || visited[indices[i]])
		{
			indices[i] += 1;
			if (indices[i] >= len && !chg)
			{
				indices[i] %= len;
				chg = true;
			}
			else if (chg)
			{
				indices[i] = rand() % len;
				break;
			}
		}
		visited[indices[i]] = true;
	}



	for (int i = 1; i < len; ++i)
		buffer[indices[i - 1]] = (void*)& buffer[indices[i]];
	buffer[indices[len - 1]] = (void*)& buffer[indices[0]];

	delete[] visited;
	delete[] indices;
	return buffer;
}




/* Tworzy pointer chain z okreslona odlegloscia (stride)(bajty) pomiedzy poszczegolnymi lokalizacjami pamieci */
void** create_linear_chain(unsigned long long size, unsigned long long stride)
{
	unsigned long long len = size / SIZE_VOID;
	void** buffer = new void* [len];

	void** last = buffer;
	int count = 0;
	while (true)
	{
		char* next = (char*)last + stride;
		if (next >= (char*)buffer + size)
			break;
		*last = (void*)next;
		last = (void**)next;
		count++;
	}
	*last = (void*)buffer;
	return buffer;
}

//Wypisuje lokalizazacje wszystkich wskazninikow w zadanym buforze
volatile unsigned long long debug_chain(void** buffer)
{
	void** p = buffer;
	unsigned long long count = 0;
	cout << "Lancuch pamieci pod adresem: " << buffer << endl;
	do {
		cout << count << "  [0x" << p << "]  0x" << *p << endl;
		++count;
		if (*p < p)
			cout << " (przeskok)";
		cout << endl;
		p = (void**)* p;
	} while (p != buffer);
	cout << count << " wskaznikow w lancuchu.\n";
	return count;
}


volatile unsigned long long chain_count(unsigned long long memsize)
{
	return memsize / sizeof(void*);
}


double chase_pointers(void** buffer)
{
	void** p = (void**)buffer;
	Timer<double> timer;
	while (p != buffer)
		p = (void**)* p;

	auto elapsed = timer.elapsed();
	chase_pointers_global = *p;
	return 0;
}

double chase_pointers(void** buffer, unsigned long long count)
{
	void** p = (void**)buffer;
	Timer<double> timer;
	while (count-- > 0)
		p = (void**)* p;

	auto elapsed = timer.elapsed();
	chase_pointers_global = *p;
	return 0;
}


//W³aœciwa funkcja pomiarowa wykorzystuj¹ca procedurê rdtscp
volatile double chase_pointers_rdtsc(void** buffer, unsigned long long count, int distance)
{
	void** p = (void**)buffer;
	int* ptr;
	char* ptr2 = (char*)buffer;
	int val;
	unsigned ui;
	uint64_t t1, t2;
	while (count-- > 0)
		p = (void**)* p;
	chase_pointers_global = *p;
	ptr = (int*)(buffer + (distance / SIZE_VOID));
	ptr2 = (char*)ptr;

	t1 = __rdtscp(&ui);
	t2 = __rdtscp(&ui);


	/*ABI WINDOWSOWE WYMAGA ZACHOWANIA:
			RDI/EDI			- Zachowany
			RSI/ESI			- Zachowany
			RBX/EBX
			RBP/EBP

	W poni¿szym ci¹gu instrukcji modyfikowane s¹ wy³¹cznie rejestry:
			EAX,EDX i ECX	przez instrukcje rdtscp
			EDI				jako miejsce przechowywania adresu pamieci
	*/
	__asm {
		push esi
		push edi

		mov edi, dword ptr[ptr2]
		rdtscp
		mov dword ptr[t1], eax
		mov edi, dword ptr[edi]
		rdtscp
		mov dword ptr[t2], eax

		pop edi
		pop esi
	}

	return t2 - t1;
}

//Funkcja testuje czasy dostêpu do wskaznikow losowo polaczonych na wskazanym obszarze
volatile void random_chase_benchmark()
{
	ofstream file;
	string s = to_string(BENCH_NUM);
	string filename = "random_chase_benchmark.csv";
	file.open(filename);
	double time;
	double bench_time = 0;
	cout.width(15);
	cout << "MEMSIZE";
	cout.width(15);
	cout << "TIME IN NS" << endl;
	for (unsigned long long memsize = MIN_SIZE; memsize <= MAX_SIZE; memsize *= 2)
	{
		void** buffer = create_random_chain(memsize);
		unsigned long long count = pow(2, 30);
		time = chase_pointers(buffer, count) * 1000000000 / count;
		delete[] buffer;
		file << memsize << "," << time << endl;
		cout.width(15);
		cout << memsize;
		cout.width(15);
		cout << time << endl;
		bench_time += time;
	}
	file.close();
	cout<<"     Completed the Random-Chase-Benchmark in "<<bench_time/1000000<<"seconds!"<<endl<<endl<<endl;
}

//Funkcja testuje czas dostepu dla lancucha wskaznikow o zadanym stride
volatile void linear_chase_benchmark()
{
	ofstream file;
	string s = to_string(BENCH_NUM);
	string filename = "linear_chase_benchmark.csv";
	file.open(filename);

	double time;
	double bench_time = 0;
	unsigned int pointer_num = 1024;
	//file<<"-------------LINEARCHAIN TESTING-----------------\n";
	//file<<"     memsize    time in microseconds    stride\n";
	for (size_t stride = MIN_STRIDE_SIZE; stride <= MAX_STRIDE_SIZE; stride += SIZE_VOID)
	{
		size_t memsize = stride * pointer_num * SIZE_VOID;
		time = 0;
		for (int i = 0; i < BENCH_NUM; i++)
		{
			void** buffer = create_linear_chain(memsize, stride);
			time += chase_pointers(buffer);
			bench_time += time;
			delete[] buffer;
		}
		time /= BENCH_NUM;
		//file<<memsize<<","<<time<<","<<stride<<endl;
		file << stride << "," << time << endl;
		memsize *= 2;
	}
	file.close();
	cout << "     Completed the Linear-Chase-Benchmark in " << bench_time / 1000000 << "seconds!" << endl << endl;
}


//Funkcja bada czasy dostêpu przy zmianie struktur pamiêci okreœlonych dan¹ odleg³oœci¹ i zapisuje wyniki do plikow
void test_distances_to_file(int n)
{
	unsigned long long test_size;
	string filename;
	ofstream file;
	for (int i = 0; i < 5; i++)
	{
		if (i == 4)
			test_size = pow(2, 29);
		else
			test_size = distances[i] * 2;

		switch (i)
		{
		case 0: filename = "LINE"; break;
		case 1: filename = "COLUMN"; break;
		case 2: filename = "ROW"; break;
		case 3: filename = "BANK"; break;
		case 4: filename = "BANK_GROUP"; break;
		}
		filename += ".txt";
		file.open(filename);


		for (int j = 0; j < n; j++)
		{
			void** buffer = create_random_chain(test_size);
			file << chase_pointers_rdtsc(buffer, pow(2, 25), distances[i]) << endl;
			delete[] buffer;
		}
		file.close();
		cout << "Completed: " << filename << endl;
	}

}

//Funkcja wczytuj¹ca zadane odleg³osci z pliku 'rozmiary.txt'
void read_size()
{
	ifstream file;
	file.open("rozmiary.txt");
	string filename;
	int power, offset;
	for (int i = 0; i < 5; i++)
	{
		switch (i)
		{
		case 0: filename = "LINE"; break;
		case 1: filename = "COLUMN"; break;
		case 2: filename = "ROW"; break;
		case 3: filename = "BANK"; break;
		case 4: filename = "BANK_GROUP"; break;
		}
		file >> power;
		file >> offset;
		cout.width(30);
		cout << filename << "  2 ^ " << power << "   +  " << offset << "  * SIZE_VOID" << endl;
		distances[i] = pow(2, power) + offset * SIZE_VOID;
		cout << distances[i] << endl;
	}
	file.close();
}



int main()
{
	read_size();
	int n;
	cout << "Wprowadz liczbe pomiarow do wykonania:";
	cin >> n;
	test_distances_to_file(n);
}
