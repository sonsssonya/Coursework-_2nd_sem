#include <iostream>
#include <vector>
#include <string>
#include <iomanip>
#include <cmath>
#include <ctime>
#include <limits>
#include <algorithm>

// ----------------------------------------------------------------------
// Вспомогательные функции
// ----------------------------------------------------------------------
inline double clamp(double val, double lo, double hi) {
    return (val < lo) ? lo : (val > hi) ? hi : val;
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
            throw std::domain_error("Initial energy out of range [0, E_max]");
        E_current_ = E0;
    }

    double getCurrentEnergy() const { return E_current_; }

    // Прогноз свободной энергии на numDays вперёд при заданной загрузке
    std::vector<double> predictFreeEnergy(const std::vector<double>& plannedLoadHours) {
        double E = E_current_;
        std::vector<double> predicted;
        predicted.reserve(plannedLoadHours.size());

        for (double loadHours : plannedLoadHours) {
            double u = clamp(loadHours / 24.0, 0.0, 1.0);
            const double dt = 1.0; // один день
            if (u == 0.0) {
                // чистое восстановление
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

    // Поиск оптимального дня для новой задачи (перебираем дни от 0 до deadline)
    int suggestOptimalDay(double taskLoadHours,
        int deadlineDay,
        const std::vector<double>& plannedLoad)
    {
        if (deadlineDay < 0 || deadlineDay >= static_cast<int>(plannedLoad.size()))
            throw std::out_of_range("deadlineDay out of plannedLoad range");

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
// 2. Работа с датами и днями недели
// ----------------------------------------------------------------------
struct Date {
    int year, month, day;
};

// Преобразует std::tm в день недели (0 = воскресенье, 6 = суббота)
int dayOfWeekFromTm(const std::tm& t) {
    return t.tm_wday; // 0=Sun,...,6=Sat
}

// Создаёт std::tm для указанной даты (нормализует)
std::tm makeDate(int year, int month, int day) {
    std::tm t = {};
    t.tm_year = year - 1900;
    t.tm_mon = month - 1;
    t.tm_mday = day;
    // вызов mktime для нормализации и вычисления дня недели
    std::mktime(&t);
    return t;
}

// Возвращает строку с датой в формате ДД.ММ.ГГГГ
std::string dateToString(const Date& d) {
    char buf[11];
    snprintf(buf, sizeof(buf), "%02d.%02d.%04d", d.day, d.month, d.year);
    return std::string(buf);
}

// Возвращает краткое название дня недели
const char* dayOfWeekShortName(int wday) {
    static const char* names[] = { "Вс","Пн","Вт","Ср","Чт","Пт","Сб" };
    return names[wday % 7];
}

// ----------------------------------------------------------------------
// 3. Класс "Умный ежедневник"
// ----------------------------------------------------------------------
class SmartDiary {
public:
    SmartDiary(const Date& start, const Date& end,
        double alpha, double beta, double maxE, double initialE,
        const std::vector<double>& weeklyLoadTemplate)
        : model_(alpha, beta, maxE),
        startDate_(start),
        endDate_(end)
    {
        model_.setInitialEnergy(initialE);

        // Вычисляем количество дней
        totalDays_ = daysBetween(startDate_, endDate_) + 1; // включаем последний день
        // Инициализируем даты, дни недели, базовую нагрузку
        dates_.resize(totalDays_);
        weekDays_.resize(totalDays_);
        baseLoad_.resize(totalDays_, 0.0);
        addedLoad_.resize(totalDays_, 0.0);

        for (int i = 0; i < totalDays_; ++i) {
            Date d = addDays(startDate_, i);
            dates_[i] = d;
            std::tm t = makeDate(d.year, d.month, d.day);
            weekDays_[i] = t.tm_wday; // 0=Sun

            // Базовая нагрузка по дню недели из шаблона (weeklyLoadTemplate[0]=Вс,...[6]=Сб)
            if (!weeklyLoadTemplate.empty()) {
                int wd = weekDays_[i];
                if (wd >= 0 && wd < 7)
                    baseLoad_[i] = weeklyLoadTemplate[wd];
            }
        }
    }

    // Получить текущую суммарную нагрузку на каждый день
    std::vector<double> getTotalLoad() const {
        std::vector<double> total(totalDays_);
        for (int i = 0; i < totalDays_; ++i)
            total[i] = baseLoad_[i] + addedLoad_[i];
        return total;
    }

    // Прогноз энергии на основе текущих задач
    std::vector<double> getEnergyForecast() {
        return model_.predictFreeEnergy(getTotalLoad());
    }

    // Добавить задачу: ищет лучший день и при успехе добавляет нагрузку
    bool addTask(const std::string& name, double hours, const Date& deadlineDate) {
        int deadlineIdx = dayIndex(deadlineDate);
        if (deadlineIdx < 0) {
            std::cout << "Ошибка: дедлайн вне календарного периода.\n";
            return false;
        }
        int bestDay = model_.suggestOptimalDay(hours, deadlineIdx, getTotalLoad());
        if (bestDay < 0) {
            std::cout << "Невозможно вписать задачу \"" << name << "\" без критического истощения.\n";
            return false;
        }

        // Добавляем задачу
        Task task;
        task.name = name;
        task.hours = hours;
        task.deadlineDay = deadlineIdx;
        task.assignedDay = bestDay;
        task.completed = false;
        tasks_.push_back(task);

        addedLoad_[bestDay] += hours;
        std::cout << "Задача \"" << name << "\" назначена на "
            << dateToString(dates_[bestDay])
            << " (" << dayOfWeekShortName(weekDays_[bestDay]) << ")\n";
        return true;
    }

    // Вывести календарь с нагрузкой и прогнозом энергии
    void printCalendar() {
        auto energy = getEnergyForecast();
        auto totalLoad = getTotalLoad();

        std::cout << "\nКалендарь занятости и прогноз энергии\n";
        std::cout << "-------------------------------------------------------------\n";
        std::cout << "Дата       | День | Нагрузка(ч) | Свободная ёмкость\n";
        std::cout << "-------------------------------------------------------------\n";

        for (int i = 0; i < totalDays_; ++i) {
            std::cout << std::left << std::setw(10) << dateToString(dates_[i]) << " | "
                << std::setw(3) << dayOfWeekShortName(weekDays_[i]) << " | "
                << std::setw(11) << totalLoad[i] << " | "
                << std::fixed << std::setprecision(3) << energy[i] << "\n";
        }
        std::cout << "-------------------------------------------------------------\n";
        if (!tasks_.empty()) {
            std::cout << "\nСписок задач:\n";
            for (const auto& t : tasks_) {
                std::cout << " - " << t.name << " (" << t.hours << " ч), дедлайн: "
                    << dateToString(dates_[t.deadlineDay])
                    << ", назначено на: " << dateToString(dates_[t.assignedDay]) << "\n";
            }
        }
    }

private:
    struct Task {
        std::string name;
        double hours;
        int deadlineDay;   // индекс дня дедлайна
        int assignedDay;    // индекс дня, когда выполнять
        bool completed;
    };

    EnergyModel model_;
    Date startDate_, endDate_;
    int totalDays_;

    std::vector<Date> dates_;
    std::vector<int>  weekDays_;     // 0=Вс..6=Сб
    std::vector<double> baseLoad_;   // запланированные часы по шаблону
    std::vector<double> addedLoad_;  // суммарные часы добавленных задач на день
    std::vector<Task> tasks_;

    // Количество дней между датами (from <= to)
    static int daysBetween(const Date& from, const Date& to) {
        std::tm start = makeDate(from.year, from.month, from.day);
        std::tm end = makeDate(to.year, to.month, to.day);
        std::time_t t1 = std::mktime(&start);
        std::time_t t2 = std::mktime(&end);
        const double secondsPerDay = 86400.0;
        return static_cast<int>(std::difftime(t2, t1) / secondsPerDay);
    }

    // Прибавить дни к дате
    static Date addDays(const Date& d, int days) {
        std::tm t = makeDate(d.year, d.month, d.day);
        t.tm_mday += days;
        std::mktime(&t); // нормализация
        return { t.tm_year + 1900, t.tm_mon + 1, t.tm_mday };
    }

    // Индекс дня по дате (или -1, если вне диапазона)
    int dayIndex(const Date& d) const {
        for (int i = 0; i < totalDays_; ++i)
            if (dates_[i].year == d.year && dates_[i].month == d.month && dates_[i].day == d.day)
                return i;
        return -1;
    }
};

// ----------------------------------------------------------------------
// 4. Консольное меню
// ----------------------------------------------------------------------
int main() {
    setlocale(LC_ALL, "russian");
    // Настройки периода
    Date startDate = { 2026, 5, 1 };   // 1 мая 2026
    Date endDate = { 2026, 7, 1 };   // 1 июля 2026

    std::cout << "=== УМНЫЙ ЕЖЕДЕВНИК ===\n";
    std::cout << "Период: " << dateToString(startDate) << " – " << dateToString(endDate) << "\n\n";

    // Ввод параметров модели
    double alpha, beta, maxE, initialE;
    std::cout << "Параметры модели восстановления энергии:\n";
    std::cout << "  Коэффициент утомления alpha: ";
    std::cin >> alpha;
    std::cout << "  Коэффициент восстановления beta: ";
    std::cin >> beta;
    std::cout << "  Максимальная ёмкость (E_max): ";
    std::cin >> maxE;
    std::cout << "  Начальная энергия (от 0 до " << maxE << "): ";
    std::cin >> initialE;

    // Ввод недельного шаблона занятости (часы по дням: Вс Пн Вт Ср Чт Пт Сб)
    std::vector<double> weekTemplate(7, 0.0);
    std::cout << "\nВведите типовую учебную нагрузку (часы) по дням недели:\n";
    const char* dayNames[] = { "Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб" };
    for (int i = 0; i < 7; ++i) {
        std::cout << "  " << dayNames[i] << ": ";
        std::cin >> weekTemplate[i];
    }

    // Создаём ежедневник
    SmartDiary diary(startDate, endDate, alpha, beta, maxE, initialE, weekTemplate);

    // Основное меню
    int choice = 0;
    do {
        std::cout << "\nМЕНЮ:\n"
            << "1. Показать календарь и прогноз энергии\n"
            << "2. Добавить задачу\n"
            << "0. Выход\n"
            << "Ваш выбор: ";
        std::cin >> choice;

        if (choice == 1) {
            diary.printCalendar();
        }
        else if (choice == 2) {
            std::string name;
            double hours;
            int d, m;
            std::cout << "Название задачи: ";
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::getline(std::cin, name);
            std::cout << "Трудоёмкость (часы): ";
            std::cin >> hours;
            std::cout << "Дедлайн (день месяц, например 15 6 для 15.06.2026): ";
            std::cin >> d >> m;
            Date deadline = { 2026, m, d };
            diary.addTask(name, hours, deadline);
        }
    } while (choice != 0);

    std::cout << "До свидания!\n";
    return 0;
}