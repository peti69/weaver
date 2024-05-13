#include <functional>

#include "sml.h"

bool SmlFile::parse(string content)
{
	std::function<void(int&, SmlNode&)> parse = [&](int& pos, SmlNode& parent)
	{
		if (pos >= content.length())
			throw std::invalid_argument("SML parsing - Data missing");
		int len = Byte(content[pos]) & 0x0F;
		switch (Byte(content[pos]) & 0xF0)
		{
			case 0x70:
			{
				pos++;
				SmlNode& item = parent.addItem(SmlNode::Sequence());
				for (int i = 0; i < len; i++)
					parse(pos, item);
				break;
			}
			case 0x00:
				if (len > 0)
				{
					if (pos + len > content.length())
						throw std::invalid_argument("SML parsing - Data missing");
					if (len == 1)
						parent.addItem(SmlNode::Null());
					else
						parent.addItem(content.substr(pos + 1, len - 1));
					pos += len;
				}
				break;
			case 0x60:
			{
				if (pos + len > content.length())
					throw std::invalid_argument("SML parsing - Data missing");
				SmlNode::Integer ui = 0;
				for (int i = 1; i < len; i++)
					ui = ui * 256 + Byte(content[pos + i]);
				parent.addItem(ui);
				pos += len;
				break;
			}
			case 0x50:
			{
				if (pos + len > content.length())
					throw std::invalid_argument("SML parsing - Data missing");
				SmlNode::Integer si = 0, factor = 1;
				for (int i = 1; i < len; i++)
				{
					si = si * 256 + Byte(content[pos + i]);
					factor *= 256;
				}
				if (content[pos + 1] & 0x80)
					si = -1 * factor  + si;
				parent.addItem(si);
				pos += len;
				break;
			}
			case 0x40:
				if (pos + len > content.length())
					throw std::invalid_argument("SML parsing - Data missing");
				parent.addItem(content[pos + 1] != 0x00);
				pos += len;
				break;
			default:
				throw std::invalid_argument("SML parsing - Unknown type length");
				break;
		}
	};

	errorText.clear();
	root = SmlNode{SmlNode::Sequence()};

	try
	{
		int pos = 0;
		while (pos < content.length())
		{
			parse(pos, root);
			if (pos >= content.length() || content[pos] != 0x00)
				throw std::invalid_argument("SML parsing - No end of message indicator");
			pos++;
		}

		return true;
	}
	catch (const std::exception& e)
	{
		errorText = e.what();
		root = SmlNode{SmlNode::Null()};

		return false;
	}
}

const SmlNode::Sequence* SmlFile::searchSequence(string value) const
{
	std::function<const SmlNode::Sequence*(const SmlNode*)> search = [&](const SmlNode* node) -> const SmlNode::Sequence*
	{
		if (auto sequence = std::get_if<SmlNode::Sequence>(&node->value); sequence && sequence->size())
		{
			if (auto str = std::get_if<SmlNode::String>(&sequence->at(0)->value); str && *str == value)
				return sequence;
			else
			{
				for (auto item : *sequence)
					if (auto sequence = search(item.get()))
						return sequence;
			}
		}
		return nullptr;
	};

	return search(&root);
}

void SmlFile::print() const
{
	std::function<void(int, const SmlNode&)> print = [&](int depth, const SmlNode& node)
	{
		if (auto sequence = std::get_if<SmlNode::Sequence>(&node.value))
		{
			std::cout << string(depth * 3, ' ') << "SEQUENCE" <<std::endl;
			for (auto item : *sequence)
				print(depth + 1, *item);
		}
		else if (std::get_if<SmlNode::Null>(&node.value))
			std::cout << string(depth * 3, ' ') << "NULL" << std::endl;
		else if (auto str = std::get_if<SmlNode::String>(&node.value))
			std::cout << string(depth * 3, ' ') << "STRING 0x" << cnvToHexStr(*str) << std::endl;
		else if (auto integer = std::get_if<SmlNode::Integer>(&node.value))
			std::cout << string(depth * 3, ' ') << "INTEGER " << *integer << std::endl;
		else if (auto boolean = std::get_if<SmlNode::Boolean>(&node.value))
			std::cout << string(depth * 3, ' ') << "BOOLEAN " << *boolean << std::endl;
	};

	print(0, root);
}

