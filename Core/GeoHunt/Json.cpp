// Geodrive Core - GPL (see Core/README.md)

#include "Json.h"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <system_error>

namespace geojson
{
	const Value* Value::Find(const std::string& Key) const
	{
		if (Kind != Type::Object)
		{
			return nullptr;
		}
		for (const auto& [Name, Field] : Fields)
		{
			if (Name == Key)
			{
				return &Field;
			}
		}
		return nullptr;
	}

	Value& Value::Set(const std::string& Key, Value V)
	{
		Kind = Type::Object;
		for (auto& [Name, Field] : Fields)
		{
			if (Name == Key)
			{
				Field = std::move(V);
				return Field;
			}
		}
		Fields.emplace_back(Key, std::move(V));
		return Fields.back().second;
	}

	namespace
	{
		struct Reader
		{
			const std::string& Text;
			size_t At = 0;
			std::string Error;

			bool Fail(const std::string& Why)
			{
				if (Error.empty())
				{
					// (the line it's on, for the one fixing the file)
					int Line = 1;
					for (size_t i = 0; i < At && i < Text.size(); ++i)
					{
						Line += Text[i] == '\n' ? 1 : 0;
					}
					Error = Why + " (line " + std::to_string(Line) + ")";
				}
				return false;
			}

			void Space()
			{
				while (At < Text.size() && (Text[At] == ' ' || Text[At] == '\t' || Text[At] == '\n' || Text[At] == '\r'))
				{
					++At;
				}
			}

			bool Word(const char* W)
			{
				const size_t N = std::char_traits<char>::length(W);
				if (Text.compare(At, N, W) != 0)
				{
					return false;
				}
				At += N;
				return true;
			}

			static void Utf8(std::string& Out, uint32_t C)
			{
				if (C < 0x80) { Out += char(C); }
				else if (C < 0x800) { Out += char(0xC0 | (C >> 6)); Out += char(0x80 | (C & 0x3F)); }
				else if (C < 0x10000) { Out += char(0xE0 | (C >> 12)); Out += char(0x80 | ((C >> 6) & 0x3F)); Out += char(0x80 | (C & 0x3F)); }
				else { Out += char(0xF0 | (C >> 18)); Out += char(0x80 | ((C >> 12) & 0x3F)); Out += char(0x80 | ((C >> 6) & 0x3F)); Out += char(0x80 | (C & 0x3F)); }
			}

			bool Hex4(uint32_t& C)
			{
				if (At + 4 > Text.size())
				{
					return Fail("a \\u needs four hex digits");
				}
				C = 0;
				for (int k = 0; k < 4; ++k)
				{
					const char H = Text[At++];
					const int Digit = H >= '0' && H <= '9' ? H - '0' : H >= 'a' && H <= 'f' ? H - 'a' + 10 : H >= 'A' && H <= 'F' ? H - 'A' + 10 : -1;
					if (Digit < 0)
					{
						return Fail("a \\u needs four hex digits");
					}
					C = C * 16 + uint32_t(Digit);
				}
				return true;
			}

			bool String(std::string& Out)
			{
				++At;	// (the opening quote)
				while (At < Text.size() && Text[At] != '"')
				{
					const char C = Text[At++];
					if (C != '\\')
					{
						Out += C;
						continue;
					}
					if (At >= Text.size())
					{
						break;
					}
					const char E = Text[At++];
					switch (E)
					{
					case '"': Out += '"'; break;
					case '\\': Out += '\\'; break;
					case '/': Out += '/'; break;
					case 'b': Out += '\b'; break;
					case 'f': Out += '\f'; break;
					case 'n': Out += '\n'; break;
					case 'r': Out += '\r'; break;
					case 't': Out += '\t'; break;
					case 'u':
					{
						uint32_t Code = 0;
						if (!Hex4(Code))
						{
							return false;
						}
						// (a pair of surrogates: one character past the first 65,536)
						if (Code >= 0xD800 && Code < 0xDC00 && Text.compare(At, 2, "\\u") == 0)
						{
							At += 2;
							uint32_t Low = 0;
							if (!Hex4(Low))
							{
								return false;
							}
							Code = 0x10000 + ((Code - 0xD800) << 10) + (Low - 0xDC00);
						}
						Utf8(Out, Code);
						break;
					}
					default:
						return Fail(std::string("an unknown escape \\") + E);
					}
				}
				if (At >= Text.size())
				{
					return Fail("a string's not closed (a \" missing)");
				}
				++At;
				return true;
			}

			bool Read(Value& Out, int Depth)
			{
				if (Depth > 64)
				{
					return Fail("nested too deep");
				}
				Space();
				if (At >= Text.size())
				{
					return Fail("the file ends too soon");
				}
				const char C = Text[At];
				if (C == '{')
				{
					Out = Value::Object();
					++At;
					Space();
					if (At < Text.size() && Text[At] == '}')
					{
						++At;
						return true;
					}
					while (true)
					{
						Space();
						if (At >= Text.size() || Text[At] != '"')
						{
							return Fail("a name in quotes expected");
						}
						std::string Key;
						if (!String(Key))
						{
							return false;
						}
						Space();
						if (At >= Text.size() || Text[At] != ':')
						{
							return Fail("a : expected after \"" + Key + "\"");
						}
						++At;
						Value Field;
						if (!Read(Field, Depth + 1))
						{
							return false;
						}
						Out.Set(Key, std::move(Field));
						Space();
						if (At < Text.size() && Text[At] == ',')
						{
							++At;
							continue;
						}
						if (At < Text.size() && Text[At] == '}')
						{
							++At;
							return true;
						}
						return Fail("a , or } expected");
					}
				}
				if (C == '[')
				{
					Out = Value::List();
					++At;
					Space();
					if (At < Text.size() && Text[At] == ']')
					{
						++At;
						return true;
					}
					while (true)
					{
						Value Item;
						if (!Read(Item, Depth + 1))
						{
							return false;
						}
						Out.Items.push_back(std::move(Item));
						Space();
						if (At < Text.size() && Text[At] == ',')
						{
							++At;
							continue;
						}
						if (At < Text.size() && Text[At] == ']')
						{
							++At;
							return true;
						}
						return Fail("a , or ] expected");
					}
				}
				if (C == '"')
				{
					Out = Value(std::string());
					return String(Out.Text);
				}
				if (Word("true")) { Out = Value(true); return true; }
				if (Word("false")) { Out = Value(false); return true; }
				if (Word("null")) { Out = Value(); return true; }
				if (C == '-' || (C >= '0' && C <= '9'))
				{
					double N = 0.0;
					const auto [End, Code] = std::from_chars(Text.data() + At, Text.data() + Text.size(), N);
					if (Code != std::errc())
					{
						return Fail("a number that can't be read");
					}
					At = size_t(End - Text.data());
					Out = Value(N);
					return true;
				}
				return Fail(std::string("unexpected '") + C + "'");
			}
		};

		void Quote(std::string& Out, const std::string& S)
		{
			Out += '"';
			for (const char C : S)
			{
				switch (C)
				{
				case '"': Out += "\\\""; break;
				case '\\': Out += "\\\\"; break;
				case '\n': Out += "\\n"; break;
				case '\r': Out += "\\r"; break;
				case '\t': Out += "\\t"; break;
				default:
					if (static_cast<unsigned char>(C) < 0x20)
					{
						char Code[8];
						std::snprintf(Code, sizeof(Code), "\\u%04x", C);
						Out += Code;
					}
					else
					{
						Out += C;
					}
				}
			}
			Out += '"';
		}

		// (a list of numbers, strings or flags: on one line)
		bool Short(const Value& V)
		{
			if (!V.IsList() || V.Items.size() > 8)
			{
				return false;
			}
			for (const Value& Item : V.Items)
			{
				if (Item.IsList() || Item.IsObject())
				{
					return false;
				}
			}
			return true;
		}

		void Put(std::string& Out, const Value& V, int Indent)
		{
			switch (V.Kind)
			{
			case Value::Type::Null: Out += "null"; break;
			case Value::Type::Bool: Out += V.Flag ? "true" : "false"; break;
			case Value::Type::Number:
			{
				// (as short as reads back the same; whole numbers without a point)
				char Digits[32];
				const auto [End, Code] = std::isfinite(V.Number) ? std::to_chars(Digits, Digits + sizeof(Digits), V.Number) : std::to_chars(Digits, Digits + sizeof(Digits), 0);
				Out.append(Digits, Code == std::errc() ? End : Digits);
				break;
			}
			case Value::Type::String: Quote(Out, V.Text); break;
			case Value::Type::List:
				if (Short(V))
				{
					Out += '[';
					for (size_t i = 0; i < V.Items.size(); ++i)
					{
						Out += i > 0 ? ", " : "";
						Put(Out, V.Items[i], Indent);
					}
					Out += ']';
					break;
				}
				Out += "[\n";
				for (size_t i = 0; i < V.Items.size(); ++i)
				{
					Out.append(size_t(Indent + 1), '\t');
					Put(Out, V.Items[i], Indent + 1);
					Out += i + 1 < V.Items.size() ? ",\n" : "\n";
				}
				Out.append(size_t(Indent), '\t');
				Out += ']';
				break;
			case Value::Type::Object:
				if (V.Fields.empty())
				{
					Out += "{}";
					break;
				}
				Out += "{\n";
				for (size_t i = 0; i < V.Fields.size(); ++i)
				{
					Out.append(size_t(Indent + 1), '\t');
					Quote(Out, V.Fields[i].first);
					Out += ": ";
					Put(Out, V.Fields[i].second, Indent + 1);
					Out += i + 1 < V.Fields.size() ? ",\n" : "\n";
				}
				Out.append(size_t(Indent), '\t');
				Out += '}';
				break;
			}
		}
	}

	bool Parse(const std::string& Text, Value& Out, std::string& Error)
	{
		Reader R{ Text };
		size_t Start = 0;
		// (a UTF-8 file's mark at its start, as some editors save)
		if (Text.compare(0, 3, "\xEF\xBB\xBF") == 0)
		{
			Start = 3;
		}
		R.At = Start;
		if (!R.Read(Out, 0))
		{
			Error = R.Error;
			return false;
		}
		R.Space();
		if (R.At != Text.size())
		{
			R.Fail("more after the end");
			Error = R.Error;
			return false;
		}
		return true;
	}

	std::string Write(const Value& V)
	{
		std::string Out;
		Put(Out, V, 0);
		Out += '\n';
		return Out;
	}
}
