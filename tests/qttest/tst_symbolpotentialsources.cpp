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

	private slots:
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
};

QET_APP_TEST_MAIN(tst_symbolpotentialsources)
#include "tst_symbolpotentialsources.moc"
