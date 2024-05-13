#ifndef SML_H
#define SML_H

#include <variant>
#include <vector>
#include <memory>
#include <cassert>

#include "basic.h"

// Represents an item of the object tree generated for a SML file.
struct SmlNode
{
	using Sequence = std::vector<std::shared_ptr<SmlNode>>;
	using String = std::string;
	using Integer = signed long long;
	using Boolean = bool;
	struct Null {};

	using Value = std::variant<String, Integer, Boolean, Sequence, Null>;

	Value value;

	SmlNode(const Value& value) : value(value) {}

	// Adds to an sequence item an additional (child) item.
	SmlNode& addItem(const Value& itemValue)
	{
		assert(std::holds_alternative<Sequence>(value));
		auto itemNode = std::make_shared<SmlNode>(itemValue);
		std::get<Sequence>(value).push_back(itemNode);
		return *itemNode;
	}
};

// Represent a Smart Message File (SML) file.
class SmlFile
{
private:
	// Root of object tree after successful parsing of file content.
	SmlNode root{SmlNode::Null()};

	// Explanation on why parsing has failed.
	string errorText;

public:
	// Parses the passed file content and creates a matching object tree.
	// The return value indicates if parsing was successful or not.
	bool parse(string content);

	// Searches inside the object tree for a sequence whose first item stores the
	// passed string. Returns null in case such a sequence is not existing.
	const SmlNode::Sequence* searchSequence(string value) const;

	// Prints the object tree on standard out.
	void print() const;
};

#endif
