// t5l2csv.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <vector>
#include <set>
#include <string>
#include <map>
#include <Windows.h>
#include <Objbase.h>
#include <ShlObj.h>

const float STOICH_RATIO = 14.64f;

struct Timestamp
{
	int day, month, year, hour, minute, second, msecond;
	__int64 Tick() const
	{
		return msecond + second * 1000 + minute * 60000 + hour * 3600000 + day * 24 * 3600000;
	}
	Timestamp(): day(0), month(0),year(0),hour(0),minute(0),second(0),msecond(0) {}
	Timestamp(__int64 tick)
	{
		__int64 rest = tick;

		day = (int)(tick / 24 * 3600000);
		rest = tick % 24 * 3600000;

		hour = (int)(rest / 3600000);
		rest = rest % 3600000;

		minute = (int)(rest / 60000);
		rest = rest % 60000;

		second = (int)(rest / 1000);
		rest = rest % 1000;

		msecond = (int)(rest);		
	}

	void Save(FILE* f) const
	{
		fprintf(f, "%02d.%02d.%04d %02d:%02d:%02d.%03d",
			day, month, year, hour, minute, second, msecond);
	}
};

enum VType
{
	VT_STRING,
	VT_INTEGER,
	VT_FLOAT,
};

struct LogLine
{
	Timestamp dateTime;
	struct Item
	{
		int symbol;
		std::string value;
		double number;
		Item(): symbol(0), number(0) {}
		Item(int sym, const char* val): symbol(sym), value(val), number(0) {}
		Item(int sym, double val): symbol(sym), number(val) {}
		bool operator<(const Item& other) const { return symbol < other.symbol;}
	};
	std::set<Item> values;

	static bool isNumber(char ch)
	{
		return (ch >= '0' && ch <= '9');
	}

	double myStrtod(const char* value, const char** end, bool* frac)
	{
		double num = 0;
		bool isFrac = false;
		bool isNeg = false;
		int fracNum = 0;
		while(*value)
		{
			if(*value == '-')
			{
				if(isNeg) break;
				isNeg = true;
			}
			else if(isNumber(*value))
			{
				num = num * 10 + (*value - '0');
				if(isFrac)
					fracNum++;
			}
			else if(*value == '.' || *value == ',')
			{
				if(isFrac)
					break;
				isFrac = true;
			}
			else
				break;
			value++;
		}
		if(fracNum > 0)
		{
			num /= pow(10.0, (double)fracNum);
			*frac = true;
		}
		if(isNeg)
			num = -num;

		*end = value;
		return num;
	}

	VType AddValue(int symbol, const char* value)
	{
		const char* end;
		bool frac = false;
		double v = myStrtod(value, &end, &frac);
		if(*end)
		{
			values.insert(Item(symbol, value));
			return VT_STRING;
		}
		else
		{
			values.insert(Item(symbol, v));
			return frac ? VT_FLOAT : VT_INTEGER;
		}
	}
};

struct Symbol
{
	std::string name;
	VType type;
	int used;
};

struct Log
{
	std::vector<Symbol>	symbols;
	std::map<std::string, unsigned> symbolsMap;
	std::vector<LogLine*> lines;

	const Timestamp& GetTimestamp(int line)
	{
		return lines[line]->dateTime;
	}

	int AddSymbol(const char* symName, unsigned int hint)
	{
		if(hint >= 0 && hint < symbols.size() && symbols[hint].name == symName)
			return hint + 1;

		int sym = symbolsMap[symName];
		if(sym)
			return sym;

		int index = symbols.size() + 1;
		symbolsMap[symName] = index;
		Symbol symb;
		symb.name = symName;
		symb.type = VT_STRING;
		symb.used = 0;
		symbols.push_back(symb);
		return index;		
	}

	void AddLine(const Timestamp& dt)
	{
		LogLine* ll = new LogLine;
		ll->dateTime = dt;
		lines.push_back(ll);
	}
	int GetNumLines() const { return lines.size(); }

	const char* GetValue(int line, const char* str)
	{
		int index = symbolsMap[str];
		if(index != 0)
		{
			const LogLine* ll = lines[line];
			for(auto it = ll->values.begin(); it != ll->values.end(); ++it)
			{
				if((*it).symbol == index)
					return (*it).value.c_str();
			}
		}
		return NULL;
	}

	void SetValue(int line, const char* str, const char* val)
	{
		int index = AddSymbol(str, -1);
		LogLine* ll = lines[line];
		for(std::set<LogLine::Item>::iterator it = ll->values.begin(); it != ll->values.end(); ++it)
		{
			if((*it).symbol == index)
			{
				const_cast<LogLine::Item&>(*it).value = val;
				return;
			}
		}
		ll->AddValue(index, val);
	}

	int GetIntValue(int line, const char* str, int defValue = 0)
	{
		const char* val = GetValue(line, str);
		if(val == NULL)
			return defValue;

		return atol(val);
	}

	void SetIntValue(int line, const char* str, int value)
	{
		char buff[32];
		_itoa_s(value, buff, 10);
		SetValue(line, str, buff);
	}

	double GetFloatValue(int line, const char* str, double defValue = 0)
	{
		const char* val = GetValue(line, str);
		if(val == NULL)
			return defValue;

		return atof(val);
	}

	bool SymbolValid(unsigned int n) const
	{
		if(symbols[n-1].used != lines.size())
			n=n;
		return symbols[n-1].used == lines.size();
	}

	double ADScannerToLambda(const LogLine::Item& value)
	{
		double v = value.number / 1023.0;
		double steepness = 21.0 - 7.35;
		double afr = v * steepness + 7.35;
		double lambda = afr / STOICH_RATIO;
		return lambda;
	}

	bool SaveCSV(FILE* f)
	{
		fputs("Time,", f);

		int sn = 0;
		for(unsigned int n = 0; n < symbols.size(); n++)
		{
			if(symbols[n].used != lines.size())
				continue;

			if(sn++)
				fputc(',', f);

			std::string name = symbols[n].name;
			if(name == "DisplProt.AD_Scanner" || name == "AFR" || name == "AD_EGR" || name == "Wideband")
				name = "Lambda";

			if(name[name.size() - 1] == '!')
				fputs(name.substr(0, name.size() - 1).c_str(), f);
			else
				fputs(name.c_str(), f);
		}
		fputs("\n", f);

		__int64 start = lines[0]->dateTime.Tick();

		__int64 tt = 0;
		double lastlamb = -1;
		int lastThrottle = -1;

		for(unsigned int line = 0; line < lines.size(); line++)
		{
			const LogLine* ll = lines[line];
			__int64 t = ll->dateTime.Tick() - start;
			tt += 100;
			bool skip = false;
			double lambda = -1;
			int throttle = -1;
			bool lambdaSymbol[256];
			int symbIndex = 0;
			for(auto it = ll->values.begin(); it != ll->values.end(); ++it, ++symbIndex)
			{
				lambdaSymbol[symbIndex] = false;
				const LogLine::Item& value = *it;

				if(!SymbolValid(value.symbol))
					continue;

				const Symbol& symb = symbols[value.symbol-1];

/*				if(symb.name == "Idle")
				{
					if(value.number == 1)
					{
						skip = true;
						break;
					}
				}
				else if(symb.name == "TPSAccCyl1"||symb.name == "LoadAccCyl1"||symb.name=="TPSRetCyl1"||symb.name=="LoadRetCyl1")
				{
					if(value.number > 0)
					{
						skip = true;
						break;
					}
				}
				else if(symb.name=="TPSRetCyl1"||symb.name=="LoadRetCyl1")
				{
					if(ll->values[n].number == 0)
					{
						skip = true;
						break;
					}
				}
				else*/ 
				
				if(symb.name == "DisplProt.AD_Scanner")
				{
					//filter out unconnected AD
					if(value.number < 10)
						lambda = 0;
					else
						lambda = ADScannerToLambda(value);
					lambdaSymbol[symbIndex] = true;
				}
				else if(symb.name == "AFR" || symb.name == "AD_EGR")
				{
					lambda = value.number / STOICH_RATIO;
					lambdaSymbol[symbIndex] = true;
				}
				else if(symb.name == "Wideband")
				{
					if(value.number > 20)
						skip = true;
					else if(value.number > 5 && value.number < 20)
						lambda = value.number / STOICH_RATIO;
					else if(value.number > 0.1 && value.number < 2)
						lambda = value.number;
					lambdaSymbol[symbIndex] = true;
				}
				else if(symb.name == "Medeltrot")
				{
					throttle = (int)value.number;
				}
			}

			if(skip)
				continue;

			if(lambda > 0)
			{
				if(lambda > 1.2 || lambda < 0.6)
				{
					lastlamb = -1;
					//continue;
				}

/*				if(lastlamb > 0 && abs(lastlamb - lambda) > 0.05f)
				{
					lastlamb = lambda;
					continue;
				}
				*/
				lastlamb = lambda;
			}
			/*
			if(throttle > 0)
			{
				if(lastThrottle > 0 && abs(lastThrottle - throttle) > 2)
				{
					lastThrottle = throttle;
					continue;
				}
				lastThrottle = throttle;
			}
		*/
			fprintf(f, "%g,", (double)t / 1000.0);
			//fprintf(f, "%g,", (double)tt / 1000.0);


			unsigned int n = 0;
			for(auto it = ll->values.begin(); it != ll->values.end(); ++it,++n)
			{
				const LogLine::Item& value = *it;
				if(!SymbolValid(value.symbol))
					continue;

				const Symbol& symb = symbols[value.symbol-1];

				if(symb.type == VT_STRING)
					fputs(value.value.c_str(), f);
				else if(symb.type == VT_INTEGER)
				{
					if(symb.name == "DisplProt.AD_Scanner")
					{
						double lambda = ADScannerToLambda(value);
						fprintf(f, "%lf", lambda);
					}
					else
						fprintf(f, "%d", (int)value.number);
				}
				else if(symb.type == VT_FLOAT)
				{
					if(symb.name == "AFR" || symb.name == "AD_EGR" || symb.name == "Wideband")
					{
						double lambda = value.number / STOICH_RATIO;
						fprintf(f, "%lf", lambda);
					}
					else
						fprintf(f, "%lf", value.number);
				}
				if(n < ll->values.size() - 1)
					fputs(",", f);
			}
			fputs("\n", f);
		}
		return true;
	}

	bool Save(FILE* f)
	{
		for(unsigned int line = 0; line < lines.size(); line++)
		{
			const LogLine* ll = lines[line];
			ll->dateTime.Save(f);
			fputs("|", f);

			unsigned int n = 0;
			for(auto it = ll->values.begin(); it != ll->values.end(); ++it, ++n)
			{
				const LogLine::Item& value = *it;
				fputs(symbols[value.symbol - 1].name.c_str(), f);
				fputs("=", f);
				fputs(value.value.c_str(), f);
				if(n < ll->values.size() - 1)
					fputs("|", f);
			}
			fputs("\n", f);
		}
		return true;
	}

	bool Load(FILE* f)
	{
		char lineBuff[4096];
		int dateType = -1;
		int lineNum = 0;
		while(fgets(lineBuff, 4096, f))
		{
			if(++lineNum == 1629)
				lineNum = lineNum;

			Timestamp dateTime;

			const char* pattern[] = {
				"%d/%d/%d %d:%d:%d.%d",
				"%d-%d-%d %d:%d:%d.%d",
				"%d.%d.%d %d:%d:%d.%d"
			};

			if(dateType < 0)
			{
				for(dateType = 0; dateType < 3; dateType++)
				{
					int s = sscanf_s(lineBuff, pattern[dateType],
							&dateTime.day, &dateTime.month, &dateTime.year,
							&dateTime.hour, &dateTime.minute, &dateTime.second, &dateTime.msecond);
					if(s == 7)
						break;
				}
				if(dateType == 3)
				{
					dateType = -1;
					continue;
				}
			}
			else
			{
					int s = sscanf_s(lineBuff, pattern[dateType],
							&dateTime.day, &dateTime.month, &dateTime.year,
							&dateTime.hour, &dateTime.minute, &dateTime.second, &dateTime.msecond);
			}

			const char* ptr = strchr(lineBuff, '|');
			if(!ptr)
				break;

			LogLine* ll = new LogLine;
			ll->dateTime = dateTime;
			int parm = 0;
			bool hasRpm = false;
			while(true)
			{
				ptr++;
				const char* eq = strchr(ptr, '=');
				char fieldName[64];
				char value[64];
				if(eq)
				{
					memcpy(fieldName, ptr, eq - ptr);
					fieldName[eq - ptr] = 0;
					const char* end = strchr(ptr, '|');
					if(end)
					{
						memcpy(value, eq + 1, end - eq - 1);
						value[end - eq - 1] = 0;
						ptr = end;
					}
					else
					{
						strcpy_s(value, eq + 1);
						break;
					}
					if(!hasRpm && strcmp(fieldName, "Rpm")==0)
						hasRpm = true;
					int symb = AddSymbol(fieldName, parm++);
					VType vt = ll->AddValue(symb, value);
					symbols[symb - 1].used++;

					if(symbols[symb-1].type < vt)
						symbols[symb-1].type = vt;
				}
				else
					break;
			}
			if(!hasRpm)
				hasRpm = false;

			if(ll->values.empty())
				delete ll;
			else
				lines.push_back(ll);
		}
		return true;
	}
};

class FileDialog
{
private:
	char m_regPath[256];
	HKEY m_hKey;

public:
	FileDialog(const char* regPath)
	{
		LONG lRes = RegOpenKeyEx(HKEY_LOCAL_MACHINE, regPath, 0, KEY_ALL_ACCESS, &m_hKey);
		if(lRes != ERROR_SUCCESS)
		{
			lRes = RegCreateKeyEx(HKEY_LOCAL_MACHINE, regPath, 0L, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &m_hKey, NULL );
		}
	}
	~FileDialog()
	{
		RegCloseKey(m_hKey);
	}

	bool OpenFile(const char* regValue, const char* masks, char* filePath)
	{
	}
};

int _tmain(int argc, _TCHAR* argv[])
{
	char szFileName[MAX_PATH] = "";
	
	if(argc <= 1)
	{
		const char registryPath[] = "SOFTWARE\\TrionicLog2CSV";
		const char registryPathValue[] = "Path";
		const char registryFileValue[] = "File";
		char initPath[260] = "";
		HKEY hKey;

		HRESULT result = SHGetFolderPath(NULL, CSIDL_PERSONAL, NULL, SHGFP_TYPE_CURRENT, initPath);

		LONG lRes = RegOpenKeyEx(HKEY_LOCAL_MACHINE, registryPath, 0, KEY_ALL_ACCESS, &hKey);
		if(lRes != ERROR_SUCCESS)
		{
			lRes = RegCreateKeyEx(HKEY_LOCAL_MACHINE, registryPath, 0L, NULL, REG_OPTION_NON_VOLATILE, KEY_ALL_ACCESS, NULL, &hKey, NULL );
		}
		if(lRes == ERROR_SUCCESS)
		{
			DWORD dwBufferSize = sizeof(initPath);
			lRes = RegQueryValueEx(hKey, registryPathValue, 0, NULL, (LPBYTE)initPath, &dwBufferSize);
			if (lRes == ERROR_SUCCESS)
			{

			}
			else
			{
				lRes = RegSetValueEx( hKey, registryPathValue, 0, REG_SZ, (BYTE*)initPath, strlen(initPath)+1);
			}
		}

		OPENFILENAME ofn;

		DWORD dwBufferSize = sizeof(szFileName);
		lRes = RegQueryValueEx(hKey, registryFileValue, 0, NULL, (LPBYTE)szFileName, &dwBufferSize);


		ZeroMemory(&ofn, sizeof(ofn));
		ofn.lStructSize = sizeof(ofn); // SEE NOTE BELOW
		ofn.hwndOwner = 0;
		ofn.lpstrFilter = "Trionic7 log (*.t7l)\0*.t7l\0Trionic5 log (*.t5l)\0*.t5l\0All Files (*.*)\0*.*\0";
		ofn.lpstrFile = szFileName;
		ofn.nMaxFile = MAX_PATH;
		ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_HIDEREADONLY;
		ofn.lpstrDefExt = "t5l";

		ofn.lpstrInitialDir = initPath;
		ofn.lpstrTitle = "TrionicSuite to CSV covertor by Fido. Pick a trionic log";
		if(!GetOpenFileName(&ofn))
			return 0;

		strcpy_s(initPath, ofn.lpstrFile);
		if(strrchr(initPath, '\\'))
			*strrchr(initPath, '\\') = 0;

		RegSetValueEx( hKey, registryPathValue, 0, REG_SZ, (BYTE*)initPath, strlen(initPath)+1);
		RegSetValueEx( hKey, registryFileValue, 0, REG_SZ, (BYTE*)ofn.lpstrFile, strlen(ofn.lpstrFile)+1);
	}
	else
	{
		strcpy_s(szFileName, argv[1]);
	}

	if(strlen(szFileName) > 0)
	{
		Log log;

		FILE* f;
		
		if(fopen_s(&f, szFileName, "rt") == 0)
		{
			log.Load(f);

			char outName[512];
			strcpy_s(outName, szFileName);
			if(strrchr(outName, '.'))
				strcpy_s(strrchr(outName, '.'), 256, ".csv");

			FILE* fo;
			if(fopen_s(&fo, outName, "wt") == 0)
			{
				log.SaveCSV(fo);
				fclose(fo);
			}
			fclose(f);
		}
	}
	return 0;
}

