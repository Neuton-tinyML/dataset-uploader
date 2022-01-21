#ifndef SIMPLE_CSV
#define SIMPLE_CSV

#include <string>
#include <fstream>
#include <sstream>
#include <vector>
#include <iomanip>


#define SIMPLE_CSV_DELIMITER_SYMBOL ','


class SimpleCsvReader
{
public:
	SimpleCsvReader(const std::string& fileName): columnsCounter(10), m_DelimiterChar(SIMPLE_CSV_DELIMITER_SYMBOL)
	{
		if (!OpenFile(fileName))
		{
			throw std::runtime_error("SimpleCsvReader: File open error - \"" + fileName + "\"");
		}
	}

	~SimpleCsvReader()
	{
		CloseFile();
	}

	bool OpenFile(const std::string& fileName)
	{
		if (m_File.is_open())
		{
			CloseFile();
		}
		m_File.open(fileName);
		return m_File.is_open();
	}

	void CloseFile()
	{
		if (!m_File.is_open())
		{
			m_File.close();
		}
	}

	void SetDelimiterChar(char delimiterChar)
	{
		m_DelimiterChar = delimiterChar;
	}

	template <typename T>
	std::vector<T> GetParcedLine()
	{
		std::vector<T> returnVector;
		returnVector.reserve(columnsCounter);
		std::string readedLine;

		if (getline(m_File, readedLine))
		{

			std::istringstream sstr(readedLine);
			size_t counter = 0;
			while (!sstr.eof() && !sstr.fail())
			{
				std::string readedValue;
				getline(sstr, readedValue, m_DelimiterChar);
				T value;
				std::istringstream convertBuffer(readedValue);
				convertBuffer >> value;
				returnVector.emplace_back(value);
				++counter;
			}
			columnsCounter = counter + 1;
		}

		return returnVector;
	}

	void Rewind()
	{
		m_File.clear();
		m_File.seekg(0);
	}

private:
	std::ifstream m_File;
	size_t columnsCounter;
	char m_DelimiterChar;
};

#endif // SIMPLE_CSV
