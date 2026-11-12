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
#include "dataBase/projectdatabase.h"
#include "diagram.h"
#include "properties/potentialtypes.h"
#include "qetapptest.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/terminal.h"
#include "qetproject.h"
#include "undocommand/addgraphicsobjectcommand.h"
#include "undocommand/removediagramcommand.h"

#include <QDomDocument>
#include <QTemporaryDir>

#include <QSqlQuery>
#include <QtTest>

/**
	The default cable type a symbol declares on a terminal (the terminal
	table's potential_type) is a source of the potential its terminal is
	on, as an instance source is, and an instance source for the same
	terminal wins over it.
*/
class tst_symbolpotentialsources : public QObject
{
	Q_OBJECT

	static PotentialTypes types(PotentialTypes::Mode mode)
	{
		PotentialTypes result;
		result.setMode(mode);
		result.setTypes({{QStringLiteral("SDI"), QColor(0, 0, 255), Qt::SolidLine, 3},
						 {QStringLiteral("HDM"), QColor(0, 160, 0), Qt::SolidLine, 3},
						 {QStringLiteral("NET"), QColor(200, 0, 0), Qt::SolidLine, 3},
						 {QStringLiteral("FIB"), QColor(255, 165, 0), Qt::SolidLine, 3}});
		return result;
	}

		//A wire of a known potential, without a type of its own, between
		//two terminals of placed symbols
	static Conductor *wire(QETProject &project)
	{
		const auto groups = project.dataBase()->potentialGroups();
		for (Diagram *diagram : project.diagrams())
			for (Conductor *c : diagram->conductors())
				if (c->properties().m_potential_type.isEmpty()
						&& !groups.isUnknown(c->uuid())
						&& groups.group_of.contains(c->uuid())
						&& c->terminal1 && c->terminal2
						&& c->terminal1->parentElement()
						&& c->terminal2->parentElement()
						&& c->terminal1->parentElement()
							!= c->terminal2->parentElement())
					return c;
		return nullptr;
	}

		//What the symbol of @p t declares, as the terminal table holds it
	static bool declare(QETProject &project, Terminal *t,
						const QString &type, const QString &signal)
	{
		QSqlQuery query(project.dataBase()->database());
		query.prepare(QStringLiteral(
			"UPDATE terminal SET potential_type = :type, signal = :signal "
			"WHERE uuid = :uuid AND element_uuid = :element"));
		query.bindValue(QStringLiteral(":type"), type);
		query.bindValue(QStringLiteral(":signal"), signal);
		query.bindValue(QStringLiteral(":uuid"), t->stableUuid().toString());
		query.bindValue(QStringLiteral(":element"),
						t->parentElement()->uuid().toString());
		return query.exec() && query.numRowsAffected() == 1;
	}

		//The type and the signal a terminal row holds
	static QStringList rowOf(QETProject &project, Terminal *t)
	{
		QSqlQuery query(project.dataBase()->database());
		query.prepare(QStringLiteral(
			"SELECT potential_type, signal FROM terminal "
			"WHERE uuid = :uuid AND element_uuid = :element"));
		query.bindValue(QStringLiteral(":uuid"), t->stableUuid().toString());
		query.bindValue(QStringLiteral(":element"),
						t->parentElement()->uuid().toString());
		if (!query.exec() || !query.next()) return {};
		return {query.value(0).toString(), query.value(1).toString()};
	}

	QTemporaryDir m_dir;

	//The fixture with potential_type="SDI" and alternating signals on
	//every terminal its definitions declare, as a file of its own
	QString defaultsFile(
		const QString &fixture = QStringLiteral("every_feature.qet"))
	{
		QFile in(QFINDTESTDATA("fixtures/" + fixture));
		if (!(in.open(QIODevice::ReadOnly)))
			return QString();
		QDomDocument document;
		if (!(document.setContent(&in)))
			return QString();
		in.close();
		int count = 0;
		const QDomNodeList terminals = document.elementsByTagName(
			QStringLiteral("terminal"));
		for (int i = 0 ; i < terminals.size() ; ++i) {
			QDomElement t = terminals.at(i).toElement();
			if (t.parentNode().nodeName() != QLatin1String("description"))
				continue;
			t.setAttribute(QStringLiteral("potential_type"),
					   QStringLiteral("SDI"));
			t.setAttribute(QStringLiteral("signal"),
					   count++ % 2 ? "in" : "out");
		}
		if (!(count > 0))
			return QString();
		const QString path = m_dir.filePath(fixture);
		QFile out(path);
		if (!(out.open(QIODevice::WriteOnly)))
			return QString();
		out.write(document.toByteArray());
		out.close();

		return path;
	}

		//Remove a folio holding symbols with defaults, built or not as
		//@p unbuilt says, put it back by undo, and check that its terminal
		//rows and the types of its wires are as they were
	bool roundTrip(QETProject &project, bool unbuilt)
	{
		project.setPotentialTypes(types(PotentialTypes::Mode::AtSource));
		projectDataBase *db = project.dataBase();
		const auto rows = [&](Diagram *folio) {
			QStringList result;
			QSqlQuery query(db->database());
			query.prepare(QStringLiteral(
				"SELECT element_uuid || uuid || ':' || potential_type || "
				"':' || signal FROM terminal WHERE element_uuid IN "
				"(SELECT uuid FROM element WHERE diagram_uuid = :folio) "
				"ORDER BY 1"));
			query.bindValue(QStringLiteral(":folio"), folio->uuid().toString());
			if (query.exec())
				while (query.next()) result << query.value(0).toString();
			return result;
		};
		const auto typesIn = [&](Diagram *folio) {
			QHash<QUuid, QString> result;
			QSqlQuery query(db->database());
			query.prepare(QStringLiteral(
				"SELECT uuid FROM conductor WHERE diagram_uuid = :folio"));
			query.bindValue(QStringLiteral(":folio"), folio->uuid().toString());
			const auto derived = db->derivedPotentialTypes().type_of;
			if (query.exec())
				while (query.next()) {
					const QUuid wire(query.value(0).toString());
					if (derived.contains(wire))
						result.insert(wire, derived.value(wire));
				}
			return result;
		};
		Diagram *folio = nullptr;
		for (Diagram *d : project.folios())
			if (d && d->isLoaded() != unbuilt && !rows(d).isEmpty()
					&& !typesIn(d).isEmpty()) {
				folio = d;
				break;
			}
		if (!folio) {
			qWarning("no folio with symbol defaults, unbuilt = %d", unbuilt);
			return false;
		}
		const QStringList before = rows(folio);
		const QHash<QUuid, QString> typed = typesIn(folio);
		if (!before.first().contains(QStringLiteral(":SDI:")))
			return false;
		const bool built = folio->isLoaded();

		project.undoStack()->push(new RemoveDiagramCommand(&project, folio));
		if (!rows(folio).isEmpty())
			return false;
		project.undoStack()->undo();

		qInfo("folio %s: built before removal = %d, rows %d, wires %d",
			  unbuilt ? "unbuilt" : "built", built, int(before.size()),
			  int(typed.size()));
		if (built == unbuilt)
			return false;
		for (const QString &type : typed)
			if (type != QLatin1String("SDI")) return false;
		return rows(folio) == before && typesIn(folio) == typed;
	}

	private slots:
		/// The symbol's own <terminal> attributes reach the terminal table,
		/// whether the project is opened or a wire is drawn afterwards, and
		/// type the wire: no UPDATE of the table here
		void definitionAttributesReachTheTable()
		{
			const QString path = defaultsFile();
			QVERIFY(!path.isEmpty());
			QETProject project(path);
			project.setPotentialTypes(types(PotentialTypes::Mode::AtSource));
			Conductor *c = wire(project);
			QVERIFY(c);
				//Opened: the document path
			for (Terminal *t : {c->terminal1, c->terminal2}) {
				const QStringList row = rowOf(project, t);
				QCOMPARE(row.value(0), QStringLiteral("SDI"));
				QVERIFY(row.value(1) == QLatin1String("out")
						|| row.value(1) == QLatin1String("in"));
				QCOMPARE(row.value(1), t->signalName());
			}
			QCOMPARE(project.dataBase()->derivedPotentialTypes()
					 .type_of.value(c->uuid()), QStringLiteral("SDI"));

				//Drawn: the live path
			Terminal *ends[2] = {nullptr, nullptr};
			Diagram *folio = c->diagram();
			for (Element *e : folio->elements()) {
				for (Terminal *t : e->terminals()) {
					if (!t->conductors().isEmpty()) continue;
					if (!ends[0]) { ends[0] = t; break; }
					if (ends[0]->parentElement() != e) { ends[1] = t; break; }
				}
				if (ends[1]) break;
			}
			QVERIFY(ends[0] && ends[1]);
			auto *drawn = new Conductor(ends[0], ends[1]);
			folio->undoStack().push(
				new AddGraphicsObjectCommand(drawn, folio, QPointF()));
			for (Terminal *t : ends) {
				const QStringList row = rowOf(project, t);
				QCOMPARE(row.value(0), QStringLiteral("SDI"));
				QVERIFY(!t->signalName().isEmpty());
				QCOMPARE(row.value(1), t->signalName());
			}
			QCOMPARE(project.dataBase()->derivedPotentialTypes()
					 .type_of.value(drawn->uuid()), QStringLiteral("SDI"));
		}

		void symbolDefaultTypesTheWire()
		{
			QETProject project(QFINDTESTDATA("fixtures/every_feature.qet"));
			project.setPotentialTypes(types(PotentialTypes::Mode::AtSource));
			Conductor *c = wire(project);
			QVERIFY(c);
			QVERIFY(declare(project, c->terminal1, "SDI", "out"));
			QVERIFY(declare(project, c->terminal2, "SDI", "in"));
			const auto derived = project.dataBase()->derivedPotentialTypes();
			QCOMPARE(derived.type_of.value(c->uuid()), QStringLiteral("SDI"));
			QVERIFY(derived.conflicts.isEmpty());
		}

		void instanceSourceOverridesSymbolDefault()
		{
			QETProject project(QFINDTESTDATA("fixtures/every_feature.qet"));
			project.setPotentialTypes(types(PotentialTypes::Mode::AtSource));
			Conductor *c = wire(project);
			QVERIFY(c);
			QVERIFY(declare(project, c->terminal1, "SDI", "out"));
			QVERIFY(declare(project, c->terminal2, "", ""));
			c->terminal1->parentElement()->setPotentialSources(
				{{c->terminal1->stableUuid(), QStringLiteral("NET")}});
			const auto derived = project.dataBase()->derivedPotentialTypes();
			QCOMPARE(derived.type_of.value(c->uuid()), QStringLiteral("NET"));
			QVERIFY(derived.conflicts.isEmpty());
		}

		void wireOwnTypeWins()
		{
			QETProject project(QFINDTESTDATA("fixtures/every_feature.qet"));
			project.setPotentialTypes(types(PotentialTypes::Mode::AtSource));
			Conductor *c = wire(project);
			QVERIFY(c);
			QVERIFY(declare(project, c->terminal1, "SDI", "out"));
			QVERIFY(declare(project, c->terminal2, "SDI", "in"));
			ConductorProperties p = c->properties();
			p.m_potential_type = QStringLiteral("FIB");
			c->setProperties(p);
			PotentialType painted;
			QVERIFY(c->potentialTypeFormat(&painted));
			QCOMPARE(painted.name, QStringLiteral("FIB"));
		}

		void conflictGivesNoType()
		{
			QETProject project(QFINDTESTDATA("fixtures/every_feature.qet"));
			project.setPotentialTypes(types(PotentialTypes::Mode::AtSource));
			Conductor *c = wire(project);
			QVERIFY(c);
			QVERIFY(declare(project, c->terminal1, "HDM", "out"));
			QVERIFY(declare(project, c->terminal2, "SDI", "in"));
			const auto derived = project.dataBase()->derivedPotentialTypes();
			QVERIFY(!derived.type_of.contains(c->uuid()));
			const int potential = project.dataBase()->potentialGroups()
					.group_of.value(c->uuid(), -1);
			QVERIFY(derived.conflicts.contains(potential));
			QCOMPARE(derived.conflicts.value(potential),
					 (QStringList{"HDM", "SDI"}));
		}

		void modeOffIgnoresDefaults()
		{
			QETProject project(QFINDTESTDATA("fixtures/every_feature.qet"));
			project.setPotentialTypes(types(PotentialTypes::Mode::Off));
			Conductor *c = wire(project);
			QVERIFY(c);
			QVERIFY(declare(project, c->terminal1, "SDI", "out"));
			QVERIFY(declare(project, c->terminal2, "SDI", "in"));
			QVERIFY(project.derivedPotentialType(c->uuid()).isEmpty());
			QVERIFY(project.dataBase()->potentialTypeMismatches().isEmpty());
			PotentialType painted;
			QVERIFY(!c->potentialTypeFormat(&painted));
		}

		/// A folio removed and put back by undo keeps the symbols'
		/// defaults, whether it was built (it writes its rows again) or
		/// not (its kept rows are copied back)
		void removedFolioKeepsTheDefaults()
		{
			const QString path = defaultsFile();
			QVERIFY(!path.isEmpty());
				//Built: the folio writes its rows again when put back
			{
				QETProject project(path);
				QVERIFY(roundTrip(project, false));
			}
				//Not built (a project opened from a .qetz): the rows kept
				//when it was removed are copied back
			const QString many = defaultsFile(QStringLiteral("wiring_list_arrows.qet"));
			QVERIFY(!many.isEmpty());
			const QString zipped = m_dir.filePath(QStringLiteral("defaults.qetz"));
			{
				QETProject project(many);
				QString error;
				QVERIFY2(project.writeContainerFile(zipped, &error),
						 qPrintable(error));
			}
			QETProject project(zipped);
			QVERIFY2(project.unloadedFolioCount() > 0,
					 "the .qetz built every folio at the open");
			QVERIFY(roundTrip(project, true));
		}
};

QET_APP_TEST_MAIN(tst_symbolpotentialsources)
#include "tst_symbolpotentialsources.moc"
