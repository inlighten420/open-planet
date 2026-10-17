// Geodrive Core - AGPL-3.0 (see LICENSE)
//
// JSON, as the game's data files are written (a pack's creatures and weapons): a value read from text, and written
// back (an object a key a line, a short list on one line; numbers as short as they read back the same).

#pragma once

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace geojson
{
	struct Value
	{
		enum class Type { Null, Bool, Number, String, List, Object };
		Type Kind = Type::Null;
		bool Flag = false;
		double Number = 0.0;
		std::string Text;
		std::vector<Value> Items;
		std::vector<std::pair<std::string, Value>> Fields;	// (in the file's order)

		Value() = default;
		Value(bool B) : Kind(Type::Bool), Flag(B) {}
		Value(double N) : Kind(Type::Number), Number(N) {}
		Value(int N) : Kind(Type::Number), Number(N) {}
		Value(const char* S) : Kind(Type::String), Text(S) {}
		Value(const std::string& S) : Kind(Type::String), Text(S) {}
		static Value List(std::vector<Value> Of = {}) { Value V; V.Kind = Type::List; V.Items = std::move(Of); return V; }
		static Value Object() { Value V; V.Kind = Type::Object; return V; }

		bool IsNull() const { return Kind == Type::Null; }
		bool IsBool() const { return Kind == Type::Bool; }
		bool IsNumber() const { return Kind == Type::Number; }
		bool IsString() const { return Kind == Type::String; }
		bool IsList() const { return Kind == Type::List; }
		bool IsObject() const { return Kind == Type::Object; }
		/** An object's field (nullptr: none, or not an object) */
		const Value* Find(const std::string& Key) const;
		/** An object's field set (replaced if there) */
		Value& Set(const std::string& Key, Value V);
	};

	/** Text read as JSON: false (and Error: what's wrong and where) if it isn't */
	bool Parse(const std::string& Text, Value& Out, std::string& Error);
	/** A value as JSON text, indented with tabs */
	std::string Write(const Value& V);
}
