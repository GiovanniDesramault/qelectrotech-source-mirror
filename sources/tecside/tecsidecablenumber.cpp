/*
	Copyright 2006-2026 The QElectroTech Team
	This file is part of QElectroTech.

	QElectroTech is free software: you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation, either version 2 of the License, or
	(at your option) any later version.

	QElectroTech is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with QElectroTech.  If not, see <http://www.gnu.org/licenses/>.
*/
#include "tecside/tecsidecablenumber.h"

#include "cable/cable.h"
#include "cable/cablemanager.h"
#include "conductorproperties.h"
#include "dataBase/projectdatabase.h"
#include "diagram.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"

#include <QDebug>
#include <QHash>
#include <QList>
#include <QMap>
#include <QUuid>
#include <algorithm>

namespace TecsideCableNumber
{
namespace
{
	QString padded(const QString &location)
	{
		if (location.isEmpty()) return QStringLiteral("00");
		return location.rightJustified(2, QLatin1Char('0'));
	}

		//Output side first, then no direction, then input side
	int rank(TerminalData::Signal signal)
	{
		switch (signal) {
		case TerminalData::Signal::Out: return 0;
		case TerminalData::Signal::None: return 1;
		default: return 2;
		}
	}

		//One core of a multicore cable
	struct Core
	{
		Conductor *conductor = nullptr;
		ConductorProperties properties;
	};

		//What takes one number: a single wire, or a whole multicore cable
		//(then @a conductor and @a properties are those of its first core,
		//or of the first one that already holds the number)
	struct Wire
	{
		QUuid id;
		Conductor *conductor = nullptr;
		ConductorProperties properties;
		QString type;
		bool keeps = false;
		Cable *cable = nullptr;
		QList<Core> cores;
			//A chain of single wires looped through devices: all its
			//wires, and the two ends the number is read from
		QList<Core> chain;
		Terminal *from = nullptr;
		Terminal *to = nullptr;
	};

	QString locationOf(Terminal *terminal)
	{
		Element *element = terminal ? terminal->parentElement() : nullptr;
		return element ? element->elementInformations()
						 .value(QStringLiteral("location")).toString()
					   : QString();
	}

		//Whether @p text is (or starts with, for a core) a number
		//format() gave for @p code and @p seq: what the refresh wrote,
		//not what the user typed in
	bool isNumber(const QString &text, const QString &code, int seq)
	{
		if (code.isEmpty() || seq < 1) return false;
		return text.startsWith(QStringLiteral("%1-%2-").arg(
				code, QString::number(seq).rightJustified(
						  3, QLatin1Char('0'))));
	}

	TerminalData::Signal signalOf(Terminal *terminal)
	{
		const QString name = terminal ? terminal->signalName() : QString();
		if (name == QLatin1String("out")) return TerminalData::Signal::Out;
		if (name == QLatin1String("in")) return TerminalData::Signal::In;
		return TerminalData::Signal::None;
	}

		//The terminal of the same device that "X IN" is looped through
		//to, "X LINK", and the other way round
	Terminal *linkPartner(Terminal *terminal)
	{
		Element *element = terminal ? terminal->parentElement() : nullptr;
		if (!element) return nullptr;
		QString name = terminal->baseName();
		if (name.endsWith(QLatin1String(" IN")))
			name = name.chopped(3) + QLatin1String(" LINK");
		else if (name.endsWith(QLatin1String(" LINK")))
			name = name.chopped(5) + QLatin1String(" IN");
		else
			return nullptr;
		for (Terminal *t : element->terminals())
			if (t->baseName() == name) return t;
		return nullptr;
	}

		//Join the single wires of one type looped through a device
		//(speaker IN to LINK) into one chain that takes one number, read
		//from the two ends of the chain. The chain is known by the wire
		//that held its number, else by its lowest uuid.
	void joinChains(QList<Wire> &wires)
	{
		QHash<Conductor *, int> index;
		QList<int> root;
		for (int i = 0 ; i < wires.size() ; ++i) {
			index.insert(wires.at(i).conductor, i);
			root << i;
		}
		const auto find = [&root](int i) {
			while (root.at(i) != i) i = root[i] = root.at(root.at(i));
			return i;
		};
		const auto partners = [&](int i) {
			QList<int> out;
			Conductor *c = wires.at(i).conductor;
			for (Terminal *t : {c->terminal1, c->terminal2})
				if (Terminal *p = linkPartner(t))
					for (Conductor *other : p->conductors()) {
						const auto j = index.constFind(other);
						if (j != index.constEnd() && !wires.at(i).type.isEmpty()
								&& wires.at(*j).type == wires.at(i).type)
							out << *j;
					}
			return out;
		};
		for (int i = 0 ; i < wires.size() ; ++i)
			for (int j : partners(i))
				root[find(i)] = find(j);

		QMap<int, QList<int>> groups;
		for (int i = 0 ; i < wires.size() ; ++i) groups[find(i)] << i;
		QList<Wire> joined;
		for (const QList<int> &group : std::as_const(groups)) {
			if (group.size() == 1) {
				joined << wires.at(group.first());
				continue;
			}
			Wire chain = wires.at(group.first());
			QList<Terminal *> ends;
			for (int i : group) {
				const Wire &w = wires.at(i);
				chain.chain << Core{w.conductor, w.properties};
				if (w.id.toString() < chain.id.toString()) chain.id = w.id;
				for (Terminal *t : {w.conductor->terminal1, w.conductor->terminal2}) {
					Terminal *p = linkPartner(t);
					bool inside = false;
					if (p)
						for (Conductor *other : p->conductors())
							inside = inside || group.contains(index.value(other, -1));
					if (!inside) ends << t;
				}
			}
			for (int i : group) {
				const Wire &w = wires.at(i);
				if (w.properties.m_type_seq >= 1
						&& w.properties.m_type_seq_of == w.type) {
					chain.id = w.id;
					chain.conductor = w.conductor;
					chain.properties = w.properties;
					break;
				}
			}
				//Output side first, as for a single wire
			std::sort(ends.begin(), ends.end(), [](Terminal *a, Terminal *b) {
				const int ra = rank(signalOf(a)), rb = rank(signalOf(b));
				return ra != rb ? ra < rb
						: padded(locationOf(a)) < padded(locationOf(b));
			});
			if (ends.size() >= 2) {
				chain.from = ends.first();
				chain.to = ends.last();
			}
			joined << chain;
		}
		wires = joined;
	}
}

/**
	@return "CODE-seq-from-to", the number padded to three digits and each
	location to two ("00" when it is empty); empty when @p code is
*/
QString format(const QString &code, int seq,
			   const QString &from, const QString &to)
{
	if (code.isEmpty()) return QString();
	return QStringLiteral("%1-%2-%3-%4").arg(
			code, QString::number(seq).rightJustified(3, QLatin1Char('0')),
			padded(from), padded(to));
}

/**
	@return the smallest integer from 1 on that is not in @p used
*/
int nextFree(const QSet<int> &used)
{
	int seq = 1;
	while (used.contains(seq)) ++seq;
	return seq;
}

/**
	@return the locations of the two ends of a cable, the output side
	first; with no direction to tell them apart the lower location first
*/
std::pair<QString, QString> fromTo(
		const QString &loc1, TerminalData::Signal s1,
		const QString &loc2, TerminalData::Signal s2)
{
	const QString a = padded(loc1);
	const QString b = padded(loc2);
	if (rank(s1) != rank(s2))
		return rank(s1) < rank(s2) ? std::make_pair(a, b)
								   : std::make_pair(b, a);
	return b < a ? std::make_pair(b, a) : std::make_pair(a, b);
}

/**
	@return the two terminals of @p conductor, the one on the output
	side first, by the same rule as fromTo
*/
std::pair<Terminal *, Terminal *> orderedEnds(Conductor *conductor)
{
	Terminal *a = conductor->terminal1;
	Terminal *b = conductor->terminal2;
	const int rank_a = rank(signalOf(a));
	const int rank_b = rank(signalOf(b));
	if (rank_a != rank_b)
		return rank_a < rank_b ? std::make_pair(a, b)
							   : std::make_pair(b, a);
	return padded(locationOf(b)) < padded(locationOf(a))
			? std::make_pair(b, a) : std::make_pair(a, b);
}

/**
	Remember who holds which number in @p folio, just built from the file
	(lazy folios), as a refresh would: the wires the file holds keep their
	numbers against a copy pasted before the next refresh. What the
	memory already holds stays.
*/
void rememberFolio(QETProject *project, Diagram *folio)
{
	if (!project || !folio) return;
	auto &memory = project->cableNumberMemory();
	for (Conductor *c : folio->conductors()) {
		const ConductorProperties p = c->properties();
		if (!p.m_tecside_number || p.m_type_seq < 1
				|| p.m_type_seq_of.isEmpty())
			continue;
			//A multicore is remembered by its cable
		const QUuid id = p.m_cable_uuid.isNull() ? c->uuid()
												 : p.m_cable_uuid;
		if (!memory.contains(id))
			memory.insert(id, {p.m_type_seq_of, p.m_type_seq});
	}
}

/**
	Number the wires of @p project that ask for it. The number rides on
	the refresh of the derived types and is written to the wire without an
	undo step of its own, so undoing the wire takes it away as well.
	Two wires that carry the same type and number (a copy) are told apart
	by their uuid: the lower one keeps it, the others take the next free.
	A multicore cable takes one number from the same per-type counter, for
	its name and for all its cores (written to every core, so the number
	survives the loss of the first one).
*/
void refreshProject(QETProject *project)
{
	if (!project) return;
	QList<Wire> wires;
	QMap<QUuid, Wire> cables;
	QMap<QUuid, QList<QPair<QString, Core>>> cores;
	for (Diagram *folio : project->folios()) {
		if (!folio || !folio->isLoaded()) continue;
		for (Conductor *c : folio->conductors()) {
			Wire wire;
			wire.conductor = c;
			wire.properties = c->properties();
			if (!wire.properties.m_tecside_number) continue;
			wire.id = c->uuid();
			wire.type = wire.properties.m_potential_type.isEmpty()
					? project->derivedPotentialType(c->uuid())
					: wire.properties.m_potential_type;
			Cable *cable = CableManager::cableByUuid(
					project, wire.properties.m_cable_uuid);
			if (cable)
				cores[cable->uuid()] << qMakePair(
						wire.type, Core{c, wire.properties});
			else
				wires << wire;
		}
	}
	joinChains(wires);
		//A multicore takes one number for all its cores. Its type is the
		//one of its first core.
	for (auto it = cores.begin() ; it != cores.end() ; ++it) {
		auto &list = it.value();
		std::sort(list.begin(), list.end(),
				  [](const QPair<QString, Core> &a,
					 const QPair<QString, Core> &b) {
			const int sa = a.second.properties.m_cable_slot;
			const int sb = b.second.properties.m_cable_slot;
			return sa != sb ? sa < sb
					: a.second.conductor->uuid().toString()
					< b.second.conductor->uuid().toString();
		});
		Wire wire;
		wire.id = it.key();
		wire.cable = CableManager::cableByUuid(project, it.key());
		wire.type = list.first().first;
		for (const auto &core : std::as_const(list)) {
			if (core.first != wire.type)
				qWarning() << "TECside cable number: cable"
						   << wire.cable->designation()
						   << "mixes cable types" << wire.type
						   << "and" << core.first;
			wire.cores << core.second;
		}
		wire.conductor = wire.cores.first().conductor;
		wire.properties = wire.cores.first().properties;
		for (const Core &core : std::as_const(wire.cores)) {
			if (core.properties.m_type_seq >= 1
					&& core.properties.m_type_seq_of == wire.type) {
				wire.properties = core.properties;
				break;
			}
		}
		wires << wire;
	}
	if (wires.isEmpty()) return;

		//The numbers of wires in folios not built stay where they are
	QHash<QString, QSet<int>> used;
	if (projectDataBase *store = project->dataBase())
		for (const auto &number : store->unplacedCableNumbers(
				 QSet<QUuid>(cores.keyBegin(), cores.keyEnd())))
			used[number.first].insert(number.second);

		//Who keeps a number several wires hold: the one that held it at
		//the last refresh (a copy never takes it from its original), the
		//lowest uuid when none or several did
	auto &memory = project->cableNumberMemory();
	QMap<QPair<QString, int>, QString> keeper;
	QMap<QPair<QString, int>, QString> previous;
	QSet<QPair<QString, int>> several;
	const auto held = [](const Wire &w) {
		return !w.type.isEmpty() && w.properties.m_type_seq >= 1
				&& w.properties.m_type_seq_of == w.type;
	};
	for (const Wire &w : std::as_const(wires)) {
		if (!held(w) || used.value(w.type).contains(w.properties.m_type_seq))
			continue;
		const QPair<QString, int> key(w.type, w.properties.m_type_seq);
		const QString uuid = w.id.toString();
		const auto known = keeper.constFind(key);
		if (known == keeper.constEnd() || uuid < *known)
			keeper.insert(key, uuid);
		if (memory.value(w.id) == key) {
			if (previous.contains(key)) several.insert(key);
			previous.insert(key, uuid);
		}
	}
	for (auto it = previous.constBegin() ; it != previous.constEnd() ; ++it)
		if (!several.contains(it.key()))
			keeper.insert(it.key(), it.value());
	for (Wire &w : wires) {
		if (!held(w)) continue;
		w.keeps = keeper.value({w.type, w.properties.m_type_seq})
				== w.id.toString();
		if (w.keeps) used[w.type].insert(w.properties.m_type_seq);
	}

	for (Wire &w : wires) {
		int seq = -1;
		QString text;
		if (!w.type.isEmpty()) {
			seq = w.properties.m_type_seq;
			if (!w.keeps) {
				seq = nextFree(used.value(w.type));
				used[w.type].insert(seq);
			}
			Terminal *a = w.from ? w.from : w.conductor->terminal1;
			Terminal *b = w.to ? w.to : w.conductor->terminal2;
			const auto ends = fromTo(locationOf(a), signalOf(a),
									 locationOf(b), signalOf(b));
			text = format(w.type, seq, ends.first, ends.second);
		}
		if (seq >= 1)
			memory.insert(w.id, {w.type, seq});
		else
			memory.remove(w.id);

			//The number the wire held, to tell it from a typed text
		const QString held_type = w.properties.m_type_seq_of;
		const int held_seq = w.properties.m_type_seq;
		if (!w.cable) {
			const QList<Core> members = w.chain.isEmpty()
					? QList<Core>{Core{w.conductor, w.properties}} : w.chain;
			for (const Core &member : members) {
				ConductorProperties p = member.properties;
				p.m_type_seq = seq;
				p.m_type_seq_of = seq >= 1 ? w.type : QString();
					//Without a type a typed text stays: only a number
					//written here goes
				if (!text.isEmpty()
						|| isNumber(p.text, member.properties.m_type_seq_of,
									member.properties.m_type_seq))
					p.text = text;
					//A formula would write the text again
				if (!text.isEmpty())
					p.m_formula.clear();
				if (p != member.properties)
					member.conductor->setProperties(p);
			}
			continue;
		}

			//Like the numbers of single wires, the name of a cable is
			//derived and is written without an undo step of its own,
			//unless the user typed one in
			//unless the user typed one in. Without a type the name
			//derived from the number goes with it, so the number freed
			//is not listed twice
		QString name = text;
		if (!text.isEmpty() && !w.cable->designationByHand())
			w.cable->setDesignation(text);
		else if (w.cable->designationByHand()
				 && !w.cable->designation().isEmpty())
			name = w.cable->designation();
		else if (text.isEmpty() && !w.cable->designationByHand()
				 && isNumber(w.cable->designation(), held_type, held_seq))
			w.cable->setDesignation(QString());
		for (const Core &core : std::as_const(w.cores)) {
			ConductorProperties p = core.properties;
			p.m_type_seq = seq;
			p.m_type_seq_of = seq >= 1 ? w.type : QString();
			if (!name.isEmpty())
				p.text = QStringLiteral("%1/%2").arg(
						name, QString::number(p.m_cable_slot + 1));
			else if (isNumber(p.text, held_type, held_seq))
				p.text.clear();
			if (!name.isEmpty())
				p.m_formula.clear();
			if (p != core.properties)
				core.conductor->setProperties(p);
		}
	}
}
}
