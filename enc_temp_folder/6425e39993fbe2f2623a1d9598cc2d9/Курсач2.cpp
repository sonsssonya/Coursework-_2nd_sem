// Умный ежедневник с моделью энергии (дифф. уравнение),
// редактируемым расписанием и приоритетами задач.
// Компиляция: C++17 (Visual Studio: /std:c++17)

#include <iostream>
//#include <windows.h> 
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <string>
#include <cmath>
#include <ctime>
#include <limits>
#include <algorithm>
#include <cctype>
#include <locale>
#include <memory>
#include <stdexcept>
#include <cstdio>

// ----------------------------------------------------------------------
// Вспомогательные функции
// ----------------------------------------------------------------------
inline double clamp(double val, double lo, double hi) {
    return (val < lo) ? lo : (val > hi) ? hi : val;
}

double inputDouble(const std::string& prompt) {
    std::string line;
    while (true) {
        std::cout << prompt;
        std::getline(std::cin, line);
        if (line.empty()) continue;
        std::replace(line.begin(), line.end(), ',', '.');
        std::istringstream iss(line);
        iss.imbue(std::locale::classic());
        double val;
        if (iss >> val && iss.eof()) {
            return val;
        }
        std::cout << "Ошибка. Введите число (например 2.5 или 2,5).\n";
    }
}

double timeToHours(const std::string& timeStr) {
    size_t colon = timeStr.find(':');
    if (colon == std::string::npos) throw std::runtime_error("Wrong time format");
    int h = std::stoi(timeStr.substr(0, colon));
    int m = std::stoi(timeStr.substr(colon + 1));
    if (h < 0 || h > 23 || m < 0 || m > 59) throw std::runtime_error("Time out of range");
    return h + m / 60.0;
}

struct Date;
Date inputDate();

// ----------------------------------------------------------------------
// Структура интервала занятости
// ----------------------------------------------------------------------
struct Interval {
    double start;
    double end;
    std::string desc;
};

std::vector<Interval> inputDayIntervals(const std::string& dayName) {
    std::cout << dayName << ": введите занятые интервалы с описанием,\n";
    std::cout << "  формат: ЧЧ:ММ-ЧЧ:ММ описание [, ...]\n";
    std::cout << "  или пустую строку, если день свободен: ";
    std::string line;
    std::getline(std::cin, line);
    std::vector<Interval> result;
    if (line.empty()) return result;

    std::stringstream ss(line);
    std::string token;
    while (std::getline(ss, token, ',')) {
        token.erase(0, token.find_first_not_of(" \t"));
        token.erase(token.find_last_not_of(" \t") + 1);
        if (token.empty()) continue;

        size_t dash = token.find('-');
        if (dash == std::string::npos) {
            std::cout << "   Пропущен неверный интервал: " << token << "\n";
            continue;
        }
        std::string times = token.substr(0, dash);
        std::string rest = token.substr(dash + 1);
        size_t space = rest.find(' ');
        std::string endTime, desc;
        if (space == std::string::npos) {
            endTime = rest;
            desc = "";
        }
        else {
            endTime = rest.substr(0, space);
            desc = rest.substr(space + 1);
            desc.erase(0, desc.find_first_not_of(" \t"));
        }
        try {
            double s = timeToHours(times);
            double e = timeToHours(endTime);
            if (e <= s || s < 0 || e > 24) throw std::runtime_error("Invalid interval");
            result.push_back({ s, e, desc });
        }
        catch (const std::exception&) {
            std::cout << "   Ошибка в интервале " << token << ", пропущен.\n";
        }
    }
    return result;
}

// ----------------------------------------------------------------------
// 1. Модель энергии на основе дифференциального уравнения
// ----------------------------------------------------------------------
class EnergyModel {
public:
    EnergyModel(double alpha, double beta, double maxEnergy = 1.0)
        : alpha_(alpha), beta_(beta), E_max_(maxEnergy)
    {
        if (alpha <= 0 || beta <= 0 || E_max_ <= 0)
            throw std::invalid_argument("Parameters must be positive");
    }

    void setInitialEnergy(double E0) {
        if (E0 < 0 || E0 > E_max_)
            throw std::domain_error("Initial energy out of range");
        E_current_ = E0;
    }

    double getCurrentEnergy() const { return E_current_; }
    double getAlpha() const { return alpha_; }
    double getBeta() const { return beta_; }
    double getMaxEnergy() const { return E_max_; }

    std::vector<double> predictFreeEnergy(const std::vector<double>& plannedLoadHours) {
        double E = E_current_;
        std::vector<double> predicted;
        predicted.reserve(plannedLoadHours.size());
        for (double loadHours : plannedLoadHours) {
            double u = clamp(loadHours / 24.0, 0.0, 1.0);
            static const double dt = 1.0;
            if (u == 0.0) {
                E = E_max_ - (E_max_ - E) * std::exp(-beta_ * dt);
            }
            else {
                double a = alpha_ * u + beta_;
                double E_inf = (beta_ * E_max_) / a;
                E = E_inf - (E_inf - E) * std::exp(-a * dt);
            }
            predicted.push_back(E);
        }
        return predicted;
    }

    int suggestOptimalDay(double taskLoadHours, int deadlineDay,
        const std::vector<double>& plannedLoad,
        int priority = 0) {
        if (deadlineDay < 0 || deadlineDay >= static_cast<int>(plannedLoad.size()))
            throw std::out_of_range("deadlineDay out of range");

        double bestMetric = -1e9;
        int bestDay = -1;
        const double minEnergyThreshold = 0.1 * E_max_;

        for (int day = 0; day <= deadlineDay; ++day) {
            std::vector<double> modifiedLoad = plannedLoad;
            modifiedLoad[day] += taskLoadHours;
            double backupE = E_current_;
            std::vector<double> energyAfter = predictFreeEnergy(modifiedLoad);
            E_current_ = backupE;

            bool feasible = true;
            double minEnergy = E_max_;
            for (double e : energyAfter) {
                if (e < minEnergyThreshold) { feasible = false; break; }
                minEnergy = std::min(minEnergy, e);
            }
            if (!feasible) continue;

            double metric = minEnergy + 0.001 * energyAfter.back();
            if (priority > 0) {
                metric -= 0.02 * day;
            }
            if (metric > bestMetric) {
                bestMetric = metric;
                bestDay = day;
            }
        }
        return bestDay;
    }

private:
    double alpha_, beta_, E_max_;
    double E_current_ = 1.0;
};

// ----------------------------------------------------------------------
// 2. Работа с датами
// ----------------------------------------------------------------------
struct Date {
    int year, month, day;
};

std::tm makeDate(int year, int month, int day) {
    std::tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    std::mktime(&t);
    return t;
}

std::string dateToString(const Date& d) {
    char buf[11];
    snprintf(buf, sizeof(buf), "%02d.%02d.%04d", d.day, d.month, d.year);
    return std::string(buf);
}

const char* dayOfWeekShortName(int wday) {
    static const char* names[] = { "Вс","Пн","Вт","Ср","Чт","Пт","Сб" };
    return names[wday % 7];
}

Date inputDate() {
    std::string line;
    while (true) {
        std::getline(std::cin, line);
        std::istringstream iss(line);
        int d, m;
        if (iss >> d >> m && iss.eof()) {
            if (d >= 1 && d <= 31 && m >= 1 && m <= 12)
                return { 2026, m, d };
        }
        std::cout << "Неверная дата. Введите день и месяц (например 15 5): ";
    }
}

// ----------------------------------------------------------------------
// 3. Класс “Умный ежедневник”
// ----------------------------------------------------------------------
class SmartDiary {
public:
    struct Task {
        std::string name;
        double hours;
        int deadlineDay;
        int assignedDay;
        bool completed;
        int priority;
        double startTime; // в часах, если hasTime == true
        bool hasTime;
    };

    SmartDiary(const Date& start, const Date& end,
        double alpha, double beta, double maxE, double initialE,
        const std::vector<std::vector<Interval>>& weekTemplate)
        : model_(alpha, beta, maxE),
        startDate_(start),
        endDate_(end),
        alpha_(alpha), beta_(beta), maxE_(maxE), initialE_(initialE)
    {
        model_.setInitialEnergy(initialE);
        totalDays_ = daysBetween(start, end) + 1;
        dates_.resize(totalDays_);
        weekDays_.resize(totalDays_);
        schedules_.resize(totalDays_);
        baseLoad_.resize(totalDays_, 0.0);
        addedLoad_.resize(totalDays_, 0.0);

        for (int i = 0; i < totalDays_; ++i) {
            Date d = addDays(startDate_, i);
            dates_[i] = d;
            std::tm t = makeDate(d.year, d.month, d.day);
            weekDays_[i] = t.tm_wday;
            if (!weekTemplate.empty()) {
                int wd = weekDays_[i];
                if (wd >= 0 && wd < 7) {
                    schedules_[i] = weekTemplate[wd];
                    double sum = 0;
                    for (const auto& inv : schedules_[i]) sum += (inv.end - inv.start);
                    baseLoad_[i] = sum;
                }
            }
        }
    }

    double getAlpha() const { return alpha_; }
    double getBeta() const { return beta_; }
    double getMaxE() const { return maxE_; }
    double getInitialE() const { return initialE_; }
    void setInitialEnergy(double e) { model_.setInitialEnergy(e); initialE_ = e; }

    std::vector<double> getTotalLoad() const {
        std::vector<double> total(totalDays_);
        for (int i = 0; i < totalDays_; ++i)
            total[i] = baseLoad_[i] + addedLoad_[i];
        return total;
    }

    std::vector<double> getEnergyForecast() {
        return model_.predictFreeEnergy(getTotalLoad());
    }

    // Собрать все занятые промежутки в дне (расписание + задачи с временем)
    std::vector<Interval> getOccupiedIntervals(int dayIndex) const {
        std::vector<Interval> occupied = schedules_[dayIndex];
        for (const auto& t : tasks_) {
            if (t.assignedDay == dayIndex && t.hasTime) {
                occupied.push_back({ t.startTime, t.startTime + t.hours, t.name });
            }
        }
        std::sort(occupied.begin(), occupied.end(),
            [](const Interval& a, const Interval& b) { return a.start < b.start; });
        return occupied;
    }

    // Ищет первый свободный промежуток достаточной длины в дне
    double findFreeSlot(int dayIndex, double requiredHours) {
        auto occupied = getOccupiedIntervals(dayIndex);
        if (occupied.empty()) {
            double dayStart = 8.0;
            if (requiredHours <= 20.0 - dayStart) return dayStart;
            return -1;
        }
        double dayStart = 8.0;
        double dayEnd = 20.0;

        if (occupied[0].start - dayStart >= requiredHours) return dayStart;
        for (size_t i = 0; i < occupied.size() - 1; ++i) {
            double gap = occupied[i + 1].start - occupied[i].end;
            if (gap >= requiredHours) return occupied[i].end;
        }
        if (dayEnd - occupied.back().end >= requiredHours) return occupied.back().end;
        return -1;
    }

    // Проверка, свободен ли промежуток [start, start+duration] в дне
    bool isIntervalFree(int dayIndex, double start, double duration) {
        auto occupied = getOccupiedIntervals(dayIndex);
        double end = start + duration;
        for (const auto& inv : occupied) {
            if (start < inv.end && end > inv.start) return false;
        }
        return (start >= 0 && end <= 24);
    }

    bool addTask(const std::string& name, double hours, const Date& deadlineDate, int priority) {
        int deadlineIdx = dayIndex(deadlineDate);
        if (deadlineIdx < 0) {
            std::cout << "Дедлайн вне периода.\n";
            return false;
        }
        auto totalLoad = getTotalLoad();
        int bestDay = model_.suggestOptimalDay(hours, deadlineIdx, totalLoad, priority);
        if (bestDay < 0) {
            std::cout << "Невозможно добавить задачу без критического истощения.\n";
            return false;
        }

        // Определяем окончательный день
        bool dayConfirmed = false;
        while (!dayConfirmed) {
            std::cout << "Рекомендуемый день: " << dateToString(dates_[bestDay])
                << " (" << dayOfWeekShortName(weekDays_[bestDay]) << ")\n";

            // Ищем свободное окно
            double slotStart = findFreeSlot(bestDay, hours);
            if (slotStart >= 0) {
                char buf[64];
                int h1 = (int)slotStart, m1 = (int)((slotStart - h1) * 60);
                int h2 = (int)(slotStart + hours), m2 = (int)(((slotStart + hours) - h2) * 60);
                snprintf(buf, sizeof(buf), "%02d:%02d – %02d:%02d", h1, m1, h2, m2);
                std::cout << "Свободное окно: " << buf << "\n";
                std::cout << "Поставить задачу на это время? (y/n): ";
                char ans;
                std::cin >> ans;
                std::cin.ignore();
                if (ans == 'y' || ans == 'Y') {
                    // Всё хорошо, используем это время
                    Task task;
                    task.name = name;
                    task.hours = hours;
                    task.deadlineDay = deadlineIdx;
                    task.assignedDay = bestDay;
                    task.completed = false;
                    task.priority = priority;
                    task.startTime = slotStart;
                    task.hasTime = true;
                    tasks_.push_back(task);
                    addedLoad_[bestDay] += hours;
                    std::cout << "Задача \"" << name << "\" назначена на "
                        << dateToString(dates_[bestDay])
                        << " в " << buf << ".\n";
                    return true;
                }
                // Отказ от предложенного времени
                std::cout << "Хотите ввести время вручную? (y/n): ";
                char ans2;
                std::cin >> ans2;
                std::cin.ignore();
                if (ans2 == 'y' || ans2 == 'Y') {
                    std::cout << "Желаемое время начала (чч:мм): ";
                    std::string timeStr;
                    std::getline(std::cin, timeStr);
                    try {
                        double manualStart = timeToHours(timeStr);
                        if (!isIntervalFree(bestDay, manualStart, hours)) {
                            std::cout << "Этот промежуток занят. Попробуйте другой день.\n";
                            // Предложить выбор другого дня
                            break; // выйдем из цикла дня, чтобы перевыбрать день
                        }
                        Task task;
                        task.name = name;
                        task.hours = hours;
                        task.deadlineDay = deadlineIdx;
                        task.assignedDay = bestDay;
                        task.completed = false;
                        task.priority = priority;
                        task.startTime = manualStart;
                        task.hasTime = true;
                        tasks_.push_back(task);
                        addedLoad_[bestDay] += hours;
                        char buf[64];
                        int h1 = (int)manualStart, m1 = (int)((manualStart - h1) * 60);
                        int h2 = (int)(manualStart + hours), m2 = (int)(((manualStart + hours) - h2) * 60);
                        snprintf(buf, sizeof(buf), "%02d:%02d – %02d:%02d", h1, m1, h2, m2);
                        std::cout << "Задача \"" << name << "\" назначена на "
                            << dateToString(dates_[bestDay])
                            << " в " << buf << ".\n";
                        return true;
                    }
                    catch (...) {
                        std::cout << "Неверный формат времени.\n";
                        break;
                    }
                }
                // Пользователь не хочет ни предложенное, ни вручную – может, выберет другой день
                std::cout << "Хотите выбрать другой день вручную? (y/n): ";
                char ans3;
                std::cin >> ans3;
                std::cin.ignore();
                if (ans3 == 'y' || ans3 == 'Y') {
                    // Показываем подходящие дни
                    std::vector<int> suitableDays;
                    for (int d = 0; d <= deadlineIdx; ++d) {
                        auto testLoad = totalLoad;
                        testLoad[d] += hours;
                        double backup = model_.getCurrentEnergy();
                        auto energy = model_.predictFreeEnergy(testLoad);
                        model_.setInitialEnergy(backup);
                        bool ok = true;
                        for (double e : energy) if (e < 0.1 * model_.getMaxEnergy()) { ok = false; break; }
                        if (ok) suitableDays.push_back(d);
                    }
                    if (suitableDays.empty()) {
                        std::cout << "Нет других подходящих дней.\n";
                        return false;
                    }
                    std::cout << "Подходящие дни:\n";
                    for (int d : suitableDays)
                        std::cout << "  " << d << " - " << dateToString(dates_[d]) << "\n";
                    std::cout << "Введите индекс дня: ";
                    int chosen;
                    std::cin >> chosen;
                    std::cin.ignore();
                    if (std::find(suitableDays.begin(), suitableDays.end(), chosen) == suitableDays.end()) {
                        std::cout << "Недопустимый день. Операция отменена.\n";
                        return false;
                    }
                    bestDay = chosen;
                    continue; // повторяем для нового дня
                }
                std::cout << "Добавить задачу без указания времени (только нагрузка)? (y/n): ";
                char ans4;
                std::cin >> ans4;
                std::cin.ignore();
                if (ans4 == 'y' || ans4 == 'Y') {
                    Task task;
                    task.name = name;
                    task.hours = hours;
                    task.deadlineDay = deadlineIdx;
                    task.assignedDay = bestDay;
                    task.completed = false;
                    task.priority = priority;
                    task.hasTime = false;
                    tasks_.push_back(task);
                    addedLoad_[bestDay] += hours;
                    std::cout << "Задача \"" << name << "\" добавлена на "
                        << dateToString(dates_[bestDay]) << " (без точного времени).\n";
                    return true;
                }
                std::cout << "Добавление задачи отменено.\n";
                return false;
            }
            else {
                // Нет свободного окна
                std::cout << "В этом дне нет непрерывного свободного окна под " << hours << " ч.\n";
                std::cout << "Хотите ввести время вручную? (y/n): ";
                char ans;
                std::cin >> ans;
                std::cin.ignore();
                if (ans == 'y' || ans == 'Y') {
                    std::cout << "Желаемое время начала (чч:мм): ";
                    std::string timeStr;
                    std::getline(std::cin, timeStr);
                    try {
                        double manualStart = timeToHours(timeStr);
                        if (!isIntervalFree(bestDay, manualStart, hours)) {
                            std::cout << "Этот промежуток занят.\n";
                            // возврат к выбору дня
                            break;
                        }
                        Task task;
                        task.name = name;
                        task.hours = hours;
                        task.deadlineDay = deadlineIdx;
                        task.assignedDay = bestDay;
                        task.completed = false;
                        task.priority = priority;
                        task.startTime = manualStart;
                        task.hasTime = true;
                        tasks_.push_back(task);
                        addedLoad_[bestDay] += hours;
                        char buf[64];
                        int h1 = (int)manualStart, m1 = (int)((manualStart - h1) * 60);
                        int h2 = (int)(manualStart + hours), m2 = (int)(((manualStart + hours) - h2) * 60);
                        snprintf(buf, sizeof(buf), "%02d:%02d – %02d:%02d", h1, m1, h2, m2);
                        std::cout << "Задача \"" << name << "\" назначена на "
                            << dateToString(dates_[bestDay])
                            << " в " << buf << ".\n";
                        return true;
                    }
                    catch (...) {
                        std::cout << "Неверный формат времени.\n";
                        break;
                    }
                }
                // Переход к выбору другого дня или добавление без времени
                std::cout << "Выберите другой день вручную? (y/n): ";
                char ans2;
                std::cin >> ans2;
                std::cin.ignore();
                if (ans2 == 'y' || ans2 == 'Y') {
                    // показываем подходящие дни
                    std::vector<int> suitableDays;
                    for (int d = 0; d <= deadlineIdx; ++d) {
                        auto testLoad = totalLoad;
                        testLoad[d] += hours;
                        double backup = model_.getCurrentEnergy();
                        auto energy = model_.predictFreeEnergy(testLoad);
                        model_.setInitialEnergy(backup);
                        bool ok = true;
                        for (double e : energy) if (e < 0.1 * model_.getMaxEnergy()) { ok = false; break; }
                        if (ok) suitableDays.push_back(d);
                    }
                    if (suitableDays.empty()) {
                        std::cout << "Нет других подходящих дней.\n";
                        return false;
                    }
                    std::cout << "Подходящие дни:\n";
                    for (int d : suitableDays)
                        std::cout << "  " << d << " - " << dateToString(dates_[d]) << "\n";
                    std::cout << "Введите индекс дня: ";
                    int chosen;
                    std::cin >> chosen;
                    std::cin.ignore();
                    if (std::find(suitableDays.begin(), suitableDays.end(), chosen) == suitableDays.end()) {
                        std::cout << "Недопустимый день. Операция отменена.\n";
                        return false;
                    }
                    bestDay = chosen;
                    continue; // повторяем для нового дня
                }
                std::cout << "Добавить задачу без указания времени (только нагрузка)? (y/n): ";
                char ans3;
                std::cin >> ans3;
                std::cin.ignore();
                if (ans3 == 'y' || ans3 == 'Y') {
                    Task task;
                    task.name = name;
                    task.hours = hours;
                    task.deadlineDay = deadlineIdx;
                    task.assignedDay = bestDay;
                    task.completed = false;
                    task.priority = priority;
                    task.hasTime = false;
                    tasks_.push_back(task);
                    addedLoad_[bestDay] += hours;
                    std::cout << "Задача \"" << name << "\" добавлена на "
                        << dateToString(dates_[bestDay]) << " (без точного времени).\n";
                    return true;
                }
                std::cout << "Добавление задачи отменено.\n";
                return false;
            }
            // Если мы здесь, значит пользователь не выбрал время и хочет сменить день
            // Но цикл dayConfirmed не меняется, поэтому надо просто прерваться и предложить другой день
            std::cout << "Хотите выбрать другой день? (y/n): ";
            char ch;
            std::cin >> ch;
            std::cin.ignore();
            if (ch == 'y' || ch == 'Y') {
                // показываем подходящие
                std::vector<int> suitableDays;
                for (int d = 0; d <= deadlineIdx; ++d) {
                    auto testLoad = totalLoad;
                    testLoad[d] += hours;
                    double backup = model_.getCurrentEnergy();
                    auto energy = model_.predictFreeEnergy(testLoad);
                    model_.setInitialEnergy(backup);
                    bool ok = true;
                    for (double e : energy) if (e < 0.1 * model_.getMaxEnergy()) { ok = false; break; }
                    if (ok) suitableDays.push_back(d);
                }
                if (suitableDays.empty()) {
                    std::cout << "Нет других подходящих дней.\n";
                    return false;
                }
                std::cout << "Подходящие дни:\n";
                for (int d : suitableDays)
                    std::cout << "  " << d << " - " << dateToString(dates_[d]) << "\n";
                std::cout << "Введите индекс дня: ";
                int chosen;
                std::cin >> chosen;
                std::cin.ignore();
                if (std::find(suitableDays.begin(), suitableDays.end(), chosen) == suitableDays.end()) {
                    std::cout << "Недопустимый день. Операция отменена.\n";
                    return false;
                }
                bestDay = chosen;
                continue;
            }
            // Если не хочет другой день, отменяем
            std::cout << "Добавление задачи отменено.\n";
            return false;
        }
        return false;
    }

    void printCalendar() {
        auto energy = getEnergyForecast();
        auto totalLoad = getTotalLoad();
        std::cout << "\nДата       | День | Нагрузка(ч) | Своб. ёмкость\n"
            << "------------------------------------------------------\n";
        for (int i = 0; i < totalDays_; ++i) {
            std::cout << std::left << std::setw(10) << dateToString(dates_[i]) << " | "
                << std::setw(3) << dayOfWeekShortName(weekDays_[i]) << " | "
                << std::setw(11) << totalLoad[i] << " | "
                << std::fixed << std::setprecision(3) << energy[i] << "\n";
        }
        if (!tasks_.empty()) {
            std::cout << "\nЗадачи:\n";
            for (const auto& t : tasks_) {
                std::string timeInfo;
                if (t.hasTime) {
                    char buf[64];
                    int h1 = (int)t.startTime, m1 = (int)((t.startTime - h1) * 60);
                    int h2 = (int)(t.startTime + t.hours), m2 = (int)(((t.startTime + t.hours) - h2) * 60);
                    snprintf(buf, sizeof(buf), "%02d:%02d-%02d:%02d", h1, m1, h2, m2);
                    timeInfo = buf;
                }
                else {
                    timeInfo = "без времени";
                }
                std::cout << " - " << t.name << " (" << t.hours << "ч), "
                    << (t.priority ? "СРОЧНО" : "обычно")
                    << ", дедлайн: " << dateToString(dates_[t.deadlineDay])
                    << ", выполн.: " << dateToString(dates_[t.assignedDay])
                    << " (" << timeInfo << ")\n";
            }
        }
    }

    void printWeek() {
        std::cout << "Введите любую дату из интересующей недели (дд мм): ";
        Date any = inputDate();
        int idx = dayIndex(any);
        if (idx < 0) {
            std::cout << "Дата вне периода.\n";
            return;
        }
        int diff = weekDays_[idx] - 1;
        if (diff < 0) diff += 7;
        int monIdx = idx - diff;
        if (monIdx < 0) monIdx = 0;
        int sunIdx = monIdx + 6;
        if (sunIdx >= totalDays_) sunIdx = totalDays_ - 1;

        std::cout << "Неделя " << dateToString(dates_[monIdx]) << " - " << dateToString(dates_[sunIdx]) << ":\n\n";
        for (int i = monIdx; i <= sunIdx; ++i) {
            std::cout << dateToString(dates_[i]) << " (" << dayOfWeekShortName(weekDays_[i]) << "):\n";
            if (schedules_[i].empty() && addedLoad_[i] == 0) {
                std::cout << "   свободен\n";
                continue;
            }
            // Выводим интервалы расписания и задачи с временем
            auto occupied = getOccupiedIntervals(i); // уже включает задачи с временем
            if (occupied.empty()) {
                std::cout << "   свободен\n";
            }
            else {
                for (const auto& inv : occupied) {
                    char buf[32];
                    int h1 = (int)inv.start, m1 = (int)((inv.start - h1) * 60);
                    int h2 = (int)inv.end, m2 = (int)((inv.end - h2) * 60);
                    snprintf(buf, sizeof(buf), "%02d:%02d-%02d:%02d", h1, m1, h2, m2);
                    std::cout << "   " << buf << " " << inv.desc << "\n";
                }
            }
            // Нагрузка от задач без времени
            double noTimeLoad = addedLoad_[i];
            for (const auto& t : tasks_) {
                if (t.assignedDay == i && t.hasTime) noTimeLoad -= t.hours;
            }
            if (noTimeLoad > 0) {
                std::cout << "   + задачи без времени (" << noTimeLoad << " ч)\n";
            }
        }
    }

    void editDay() {
        std::cout << "Введите дату для редактирования (дд мм): ";
        Date date = inputDate();
        int idx = dayIndex(date);
        if (idx < 0) {
            std::cout << "Дата вне периода.\n";
            return;
        }
        auto& intervals = schedules_[idx];
        std::cout << "Текущие интервалы " << dateToString(date) << ":\n";
        if (intervals.empty()) {
            std::cout << "  (свободен)\n";
        }
        else {
            for (size_t i = 0; i < intervals.size(); ++i) {
                char buf[32];
                int h1 = (int)intervals[i].start, m1 = (int)((intervals[i].start - h1) * 60);
                int h2 = (int)intervals[i].end, m2 = (int)((intervals[i].end - h2) * 60);
                snprintf(buf, sizeof(buf), "%02d:%02d-%02d:%02d", h1, m1, h2, m2);
                std::cout << "  [" << i << "] " << buf << " " << intervals[i].desc << "\n";
            }
        }

        while (true) {
            std::cout << "Добавить (a), удалить (d) или отмена (c)? (a/d/c): ";
            char act;
            std::cin >> act;
            std::cin.ignore();
            if (act == 'c' || act == 'C') {
                std::cout << "Изменения отменены.\n";
                return;
            }
            else if (act == 'a') {
                std::cout << "Введите интервал (чч:мм-чч:мм описание): ";
                std::string line;
                std::getline(std::cin, line);
                size_t dash = line.find('-');
                if (dash == std::string::npos) {
                    std::cout << "Неверный формат.\n"; return;
                }
                std::string startS = line.substr(0, dash);
                std::string rest = line.substr(dash + 1);
                size_t space = rest.find(' ');
                std::string endS, desc;
                if (space == std::string::npos) {
                    endS = rest;
                    desc = "";
                }
                else {
                    endS = rest.substr(0, space);
                    desc = rest.substr(space + 1);
                    desc.erase(0, desc.find_first_not_of(" \t"));
                }
                try {
                    double s = timeToHours(startS);
                    double e = timeToHours(endS);
                    if (e <= s || s < 0 || e > 24) throw std::runtime_error("Invalid");
                    intervals.push_back({ s, e, desc });
                    baseLoad_[idx] = 0;
                    for (const auto& inv : intervals) baseLoad_[idx] += (inv.end - inv.start);
                    std::cout << "Интервал добавлен.\n";
                }
                catch (...) {
                    std::cout << "Ошибка времени.\n";
                }
                return;
            }
            else if (act == 'd') {
                int num;
                std::cout << "Номер интервала для удаления: ";
                std::cin >> num;
                std::cin.ignore();
                if (num < 0 || num >= static_cast<int>(intervals.size())) {
                    std::cout << "Неверный номер.\n"; return;
                }
                intervals.erase(intervals.begin() + num);
                baseLoad_[idx] = 0;
                for (const auto& inv : intervals) baseLoad_[idx] += (inv.end - inv.start);
                std::cout << "Интервал удалён.\n";
                return;
            }
            else {
                std::cout << "Неверная команда.\n";
            }
        }
    }

    void saveToFile(const std::string& filename) const {
        std::ofstream f(filename);
        if (!f) { std::cerr << "Ошибка записи файла\n"; return; }
        f << alpha_ << "\n" << beta_ << "\n" << maxE_ << "\n" << initialE_ << "\n";
        f << totalDays_ << "\n";
        for (int i = 0; i < totalDays_; ++i) {
            f << schedules_[i].size() << "\n";
            for (const auto& inv : schedules_[i]) {
                f << inv.start << " " << inv.end << " " << inv.desc << "\n";
            }
        }
        f << tasks_.size() << "\n";
        for (const auto& t : tasks_) {
            f << t.name << "\n"
                << t.hours << "\n"
                << t.deadlineDay << "\n"
                << t.assignedDay << "\n"
                << t.completed << "\n"
                << t.priority << "\n"
                << t.hasTime << "\n"
                << t.startTime << "\n";
        }
        std::cout << "Сохранено в " << filename << "\n";
    }

    static SmartDiary loadFromFile(const std::string& filename,
        const Date& start, const Date& end) {
        std::ifstream f(filename);
        if (!f) throw std::runtime_error("Не могу открыть файл");

        double alpha, beta, maxE, initialE;
        f >> alpha >> beta >> maxE >> initialE;
        std::vector<std::vector<Interval>> weekTemplate(7);
        SmartDiary diary(start, end, alpha, beta, maxE, initialE, weekTemplate);

        int totalDays;
        f >> totalDays;
        if (totalDays != diary.totalDays_) throw std::runtime_error("Несовпадение количества дней");
        for (int i = 0; i < totalDays; ++i) {
            int n;
            f >> n;
            f.ignore();
            std::vector<Interval> dayIntervals;
            for (int j = 0; j < n; ++j) {
                double s, e;
                std::string desc;
                f >> s >> e;
                std::getline(f >> std::ws, desc);
                dayIntervals.push_back({ s, e, desc });
            }
            diary.schedules_[i] = dayIntervals;
            double sum = 0;
            for (auto& inv : diary.schedules_[i]) sum += inv.end - inv.start;
            diary.baseLoad_[i] = sum;
        }
        int numTasks;
        f >> numTasks;
        f.ignore();
        for (int i = 0; i < numTasks; ++i) {
            Task t;
            std::getline(f, t.name);
            f >> t.hours >> t.deadlineDay >> t.assignedDay >> t.completed >> t.priority;
            // читаем новые поля, если они есть (может отсутствовать в старых файлах)
            if (f.peek() != EOF) {
                f >> t.hasTime;
                if (t.hasTime) {
                    f >> t.startTime;
                }
                else {
                    t.startTime = 0;
                }
            }
            else {
                t.hasTime = false;
                t.startTime = 0;
            }
            f.ignore();
            diary.tasks_.push_back(t);
            diary.addedLoad_[t.assignedDay] += t.hours;
        }
        return diary;
    }

private:
    EnergyModel model_;
    Date startDate_, endDate_;
    int totalDays_;
    std::vector<Date> dates_;
    std::vector<int> weekDays_;
    std::vector<std::vector<Interval>> schedules_;
    std::vector<double> baseLoad_;
    std::vector<double> addedLoad_;
    std::vector<Task> tasks_;
    double alpha_, beta_, maxE_, initialE_;

    static int daysBetween(const Date& from, const Date& to) {
        std::tm t1 = makeDate(from.year, from.month, from.day);
        std::tm t2 = makeDate(to.year, to.month, to.day);
        std::time_t time1 = std::mktime(&t1);
        std::time_t time2 = std::mktime(&t2);
        return static_cast<int>(std::difftime(time2, time1) / 86400.0);
    }

    static Date addDays(const Date& d, int days) {
        std::tm t = makeDate(d.year, d.month, d.day);
        t.tm_mday += days;
        std::mktime(&t);
        return { t.tm_year + 1900, t.tm_mon + 1, t.tm_mday };
    }

    int dayIndex(const Date& d) const {
        for (int i = 0; i < totalDays_; ++i)
            if (dates_[i].year == d.year && dates_[i].month == d.month && dates_[i].day == d.day)
                return i;
        return -1;
    }
};

// ----------------------------------------------------------------------
// Главное меню
// ----------------------------------------------------------------------
int main() {
    system("chcp 65001 > nul");
    setlocale(LC_ALL, "ru");
    Date startDate = { 2026, 5, 1 };
    Date endDate = { 2026, 7, 1 };
    const std::string saveFile = "diary_data.txt";

    std::cout << "=== УМНЫЙ ЕЖЕДНЕВНИК (01.05.2026 – 01.07.2026) ===\n";

    std::unique_ptr<SmartDiary> diaryPtr;
    double alpha, beta, maxE, initialE;

    if (std::ifstream(saveFile).good()) {
        std::cout << "Найдены сохранённые данные. Загрузить? (y/n): ";
        std::string ans;
        std::getline(std::cin, ans);
        if (!ans.empty() && std::tolower(static_cast<unsigned char>(ans[0])) == 'y') {
            try {
                diaryPtr.reset(new SmartDiary(SmartDiary::loadFromFile(saveFile, startDate, endDate)));
                alpha = diaryPtr->getAlpha();
                beta = diaryPtr->getBeta();
                maxE = diaryPtr->getMaxE();
                initialE = diaryPtr->getInitialE();
                std::cout << "Загружено.\n";
            }
            catch (const std::exception& e) {
                std::cerr << "Ошибка загрузки: " << e.what() << "\n";
                if (std::rename(saveFile.c_str(), (saveFile + ".bak").c_str()) == 0)
                    std::cout << "Старый файл сохранён как " << saveFile << ".bak\n";
                else
                    std::remove(saveFile.c_str());
                std::cout << "Начинаем чистую настройку.\n";
                diaryPtr.reset();
            }
        }
    }

    if (!diaryPtr) {
        std::cout << "\nВведите параметры модели:\n";
        alpha = inputDouble("  alpha (утомление): ");
        beta = inputDouble("  beta  (восстановление): ");
        maxE = inputDouble("  E_max (макс. ёмкость): ");
        initialE = inputDouble("  Начальная энергия (0-" + std::to_string(maxE) + "): ");

        std::cout << "\nТеперь задайте типовую неделю (интервалы с описанием).\n";
        std::cout << "Пример: 8:00-13:20 уник, 14:00-18:00 работа\n";
        const char* dayNames[] = { "Вс","Пн","Вт","Ср","Чт","Пт","Сб" };
        std::vector<std::vector<Interval>> weekTemplate(7);
        for (int i = 0; i < 7; ++i) {
            weekTemplate[i] = inputDayIntervals(dayNames[i]);
        }
        diaryPtr.reset(new SmartDiary(startDate, endDate, alpha, beta, maxE, initialE, weekTemplate));
    }

    SmartDiary& diary = *diaryPtr;

    int choice;
    do {
        std::cout << "\nМЕНЮ\n"
            << "1. Показать календарь и прогноз энергии\n"
            << "2. Посмотреть неделю\n"
            << "3. Редактировать день\n"
            << "4. Добавить задачу\n"
            << "5. Сохранить\n"
            << "0. Выход\n"
            << ">>> ";
        if (!(std::cin >> choice)) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "Введите число.\n";
            continue;
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        switch (choice) {
        case 1: diary.printCalendar(); break;
        case 2: diary.printWeek(); break;
        case 3: diary.editDay(); break;
        case 4: {
            std::string name;
            double hours;
            int d, m, priority;
            std::cout << "Название задачи: ";
            std::getline(std::cin, name);
            hours = inputDouble("Трудоёмкость (часы): ");
            std::cout << "Дедлайн (день месяц): ";
            std::cin >> d >> m;
            std::cout << "Приоритет (0 - обычно, 1 - срочно): ";
            std::cin >> priority;
            std::cin.ignore();
            Date deadline = { 2026, m, d };
            diary.addTask(name, hours, deadline, priority);
            break;
        }
        case 5:
            diary.saveToFile(saveFile);
            break;
        case 0:
            diary.saveToFile(saveFile);
            std::cout << "До свидания!\n";
            break;
        default:
            std::cout << "Неверный пункт.\n";
        }
    } while (choice != 0);

    return 0;
}