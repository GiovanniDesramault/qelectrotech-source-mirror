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
#include "tecside/tecsidematerials.h"
#include "ElementsCollection/elementslocation.h"
#include "bomexport.h"
#include "connectionsymbols.h"
#include "diagram.h"
#include "qetgraphicsitem/conductor.h"
#include "qetgraphicsitem/element.h"
#include "qetgraphicsitem/independenttextitem.h"
#include "qetgraphicsitem/terminal.h"
#include "qetinformation.h"
#include "qetproject.h"
#include "tecside/tecsidecablenumber.h"
#include "tecside/tecsidelocations.h"
#include "tecside/tecsiderackdrawer.h"
#include "undocommand/addgraphicsobjectcommand.h"
#include <QCoreApplication>
#include <QFile>
#include <QFileDialog>
#include <QHash>
#include <QMap>
#include <QMessageBox>
#include <QSet>
#include <QUndoStack>

namespace {
	QString tr(const char *text)
	{
		return QCoreApplication::translate("TecsideMaterials", text);
	}

		//The cable codes of TECside, as people say them
	QString cableName(const QString &code)
	{
		static const QHash<QString, const char *> names = {
			{QStringLiteral("S"), QT_TRANSLATE_NOOP("TecsideMaterials", "SDI")},
			{QStringLiteral("N"), QT_TRANSLATE_NOOP("TecsideMaterials", "Network")},
			{QStringLiteral("F"), QT_TRANSLATE_NOOP("TecsideMaterials", "Fiber")},
			{QStringLiteral("A"), QT_TRANSLATE_NOOP("TecsideMaterials", "Audio")},
			{QStringLiteral("SPK"), QT_TRANSLATE_NOOP("TecsideMaterials", "Speaker")},
			{QStringLiteral("D"), QT_TRANSLATE_NOOP("TecsideMaterials", "DMX / AES")},
			{QStringLiteral("AC"), QT_TRANSLATE_NOOP("TecsideMaterials", "Power")},
			{QStringLiteral("DC"), QT_TRANSLATE_NOOP("TecsideMaterials", "DC power")},
			{QStringLiteral("L"), QT_TRANSLATE_NOOP("TecsideMaterials", "LED")},
		};
		if (code.isEmpty())
			return tr("Cable without type");
		return names.contains(code) ? tr(names.value(code)) : code;
	}

	QString locationOf(const Element *element)
	{
		return element ? element->elementInformations()
						 .value(QETInformation::ELMT_LOCATION).toString().trimmed()
					   : QString();
	}

	bool isCounted(Element *element)
	{
		if (!(element->linkType() & (Element::Simple | Element::Master)))
			return false;
		if (TecsideRackDrawer::isRackSymbol(element)
			|| ConnectionSymbols::isJunction(element->location()))
			return false;
		const QString excluded = element->elementInformations()
				.value(QStringLiteral("exclude_from_bom")).toString();
		return excluded.isEmpty() || excluded == QLatin1String("false")
				|| excluded == QLatin1String("0");
	}

	QString itemOf(Element *element)
	{
		const DiagramContext info = element->elementInformations();
		const QString item = QStringLiteral("%1 %2").arg(
					info.value(QStringLiteral("manufacturer")).toString(),
					info.value(QStringLiteral("designation")).toString()).trimmed();
		return item.isEmpty() ? element->name() : item;
	}
}

QList<TecsideMaterials::Line> TecsideMaterials::lines(QETProject *project)
{
	if (!project)
		return {};
	project->loadFolios();

		//(place key, is cable, item) -> count; a run's key is its two
		//numbers, lowest first, joined by a tab
	QMap<std::tuple<QString, bool, QString>, int> counts;
	QSet<QPair<Terminal *, Terminal *>> wires;
	QSet<QUuid> cables;
	for (Diagram *folio : project->folios()) {
		if (!folio)
			continue;
		for (Element *element : folio->elements())
			if (isCounted(element))
				++counts[{locationOf(element), false, itemOf(element)}];

		for (Conductor *conductor : folio->conductors()) {
			if (!conductor->terminal1 || !conductor->terminal2)
				continue;
			const ConductorProperties p = conductor->properties();
			if (!p.m_cable_uuid.isNull()) {
				if (cables.contains(p.m_cable_uuid))
					continue;
				cables.insert(p.m_cable_uuid);
			}
				//A wire drawn through corners is several conductors with
				//the same two ends: one cable
			auto ends = TecsideCableNumber::orderedEnds(conductor);
			auto key = ends.first < ends.second ? qMakePair(ends.first, ends.second)
												: qMakePair(ends.second, ends.first);
			if (p.m_cable_uuid.isNull()) {
				if (wires.contains(key))
					continue;
				wires.insert(key);
			}
			const QString code = p.m_tecside_number ? p.m_type_seq_of
					: (p.m_potential_type.isEmpty()
					   ? project->derivedPotentialType(conductor->uuid())
					   : p.m_potential_type);
			QString item = cableName(code);
			if (!p.m_cable_uuid.isNull())
				item = tr("Multicore %1").arg(item);
			QString a = locationOf(ends.first ? ends.first->parentElement() : nullptr);
			QString b = locationOf(ends.second ? ends.second->parentElement() : nullptr);
			if (a.isEmpty() || a == b)
				a = b;
			else if (!b.isEmpty() && a != b)
				a = a < b ? a + QLatin1Char('\t') + b : b + QLatin1Char('\t') + a;
			++counts[{a, true, item}];
		}
	}

	const TecsideLocations &locations = project->tecsideLocations();
	auto labelOf = [&locations](const QString &number) {
		return locations.label({number, locations.nameOf(number)});
	};
	QHash<QString, int> order;
	for (const TecsideLocation &location : locations.list())
		order.insert(location.number, order.size());
	auto rank = [&order](const QString &key) {
		if (key.isEmpty()) return 3000000;
		if (key.contains(QLatin1Char('\t'))) return 2000000;
		return order.value(key, 1000000);
	};

	QList<std::tuple<QString, bool, QString>> keys = counts.keys();
	std::stable_sort(keys.begin(), keys.end(), [&rank](const auto &a, const auto &b) {
		if (rank(std::get<0>(a)) != rank(std::get<0>(b)))
			return rank(std::get<0>(a)) < rank(std::get<0>(b));
		return a < b;
	});
	QList<Line> lines;
	for (const auto &key : std::as_const(keys)) {
		const QString place = std::get<0>(key);
		Line line;
		if (place.isEmpty())
			line.place = tr("No location");
		else if (place.contains(QLatin1Char('\t')))
			line.place = labelOf(place.section(QLatin1Char('\t'), 0, 0)) + QStringLiteral(" ↔ ")
					+ labelOf(place.section(QLatin1Char('\t'), 1, 1));
		else
			line.place = labelOf(place);
		line.cable = std::get<1>(key);
		line.item = std::get<2>(key);
		line.count = counts.value(key);
		lines << line;
	}
	return lines;
}

QStringList TecsideMaterials::headers()
{
	return {tr("Location"), tr("Kind"), tr("Item"), tr("Quantity")};
}

QByteArray TecsideMaterials::toCsv(QETProject *project)
{
	QByteArray csv("\xEF\xBB\xBF");
	csv += BomExport::csvRecord(headers());
	for (const Line &line : lines(project))
		csv += BomExport::csvRecord({line.place, line.cable ? tr("Cable") : tr("Device"),
									 line.item, QString::number(line.count)});
	return csv;
}

Diagram *TecsideMaterials::draw(QETProject *project)
{
	if (!project || project->isReadOnly())
		return nullptr;
	const QList<Line> all = lines(project);
	if (all.isEmpty())
		return nullptr;

		//One block per place, the blocks in columns of at most 45 lines
	const int per_column = 45, column_width = 300, line_height = 14;
	QList<QStringList> columns{{}};
	QString place;
	for (const Line &line : all) {
		QStringList block;
		if (line.place != place) {
			place = line.place;
			if (!columns.last().isEmpty())
				block << QString();
			block << place;
		}
		block << QStringLiteral("   %1 ×  %2").arg(line.count).arg(
					 line.cable ? tr("Cable %1").arg(line.item) : line.item);
		if (columns.last().size() + block.size() > per_column && !columns.last().isEmpty()) {
			if (block.first().isEmpty())
				block.removeFirst();
			if (block.first().startsWith(QLatin1Char(' ')))
				block.prepend(place + tr(" (continued)"));
			columns << QStringList();
		}
		columns.last() << block;
	}
	int rows = 0;
	for (const QStringList &column : std::as_const(columns))
		rows = std::max(rows, int(column.size()));

	QUndoStack *stack = project->undoStack();
	stack->beginMacro(tr("Draw the materials list"));
	Diagram *diagram = project->addNewDiagram();
	if (!diagram) {
		stack->endMacro();
		return nullptr;
	}
	QPointF origin;
	TecsideRackDrawer::prepareFolio(diagram, tr("Materials list"),
									QSizeF(columns.size() * column_width, rows * line_height),
									&origin);
	auto *macro = new QUndoCommand();
	for (int c = 0 ; c < columns.size() ; ++c) {
		auto *text = new IndependentTextItem(columns.at(c).join(QLatin1Char('\n')));
		const QPointF pos = origin + QPointF(c * column_width, 0);
		text->setPos(pos);
		new AddGraphicsObjectCommand(text, diagram, pos, macro);
	}
	stack->push(macro);
	stack->endMacro();
	return diagram;
}

void TecsideMaterials::exportWithDialog(QETProject *project, QWidget *parent)
{
	if (!project)
		return;
	const QString path = QFileDialog::getSaveFileName(
		parent, tr("Export materials list"),
		tr("materials list_") + project->title() + QStringLiteral(".csv"),
		tr("CSV files (*.csv)"));
	if (path.isEmpty())
		return;
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
		|| file.write(toCsv(project)) < 0)
		QMessageBox::warning(parent, tr("Export materials list"),
							 tr("Unable to write %1.\n%2").arg(path, file.errorString()));
}
