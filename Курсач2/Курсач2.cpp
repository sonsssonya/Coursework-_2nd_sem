#define NOMINMAX   // <-- обязательно до всех включений

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <vector>
#include <string>
#include <cmath>
#include <ctime>
#include <limits>
#include <algorithm>
#include <cstring>
#include <locale>

#ifdef _WIN32
#include <windows.h>
#endif

// ----------------------------------------------------------------------
// Настройка консоли для корректного отображения кириллицы (Windows)
// ----------------------------------------------------------------------
void setupRussianLocale() {
#ifdef _WIN32
    SetConsoleCP(1251);
    SetConsoleOutputCP(1251);
#endif
    // Глобальная локаль для ввода/вывода (русская)
    std::locale::global(std::locale("Russian"));
    std::cout.imbue(std::locale("Russian"));
    std::cin.imbue(std::locale("Russian"));
}

// ----------------------------------------------------------------------
// Вспомогательные функции
// ----------------------------------------------------------------------
inline double clamp(double val, double lo, double hi) {
    return (val < lo) ? lo : (val > hi) ? hi : val;
}

// Безопасный ввод double с точкой (локаль C)
double inputDouble(const std::string& prompt) {
    std::string line;
    while (true) {
        std::cout << prompt;
        if (!std::getline(std::cin, line)) {
            std::cin.clear();
            continue;
        }
        // Обрезаем пробелы по краям
        line.erase(0, line.find_first_not_of(" \t"));
        line.erase(line.find_last_not_of(" \t") + 1);
        if (line.empty()) continue;

        // Заменяем запятые на точки (русская клавиатура)
        std::replace(line.begin(), line.end(), ',', '.');

        // Преобразуем с локалью "C" (точка как разделитель)
        std::istringstream iss(line);
        iss.imbue(std::locale("C"));
        double val;
        iss >> val;
        if (!iss.fail() && iss.eof()) {
            return val;
        }
        std::cout << "Ошибка ввода. Введите число (например 2.5).\n";
    }
}

// Ввод интервалов занятости для одного дня недели
// Формат: чч:мм-чч:мм[, чч:мм-чч:мм ...]
double inputDayIntervals(const std::string& dayName) {
    std::cout << dayName << ": введите занятые интервалы (чч:мм-чч:мм) через запятую,\n"
        << "или пустую строку если день свободен: ";
    std::string line;
    std::getline(std::cin, line);
    if (line.empty()) return 0.0;

    double totalHours = 0.0;
    std::stringstream ss(line);
    std::string interval;
    while (std::getline(ss, interval, ',')) {
        interval.erase(0, interval.find_first_not_of(" \t"));
        interval.erase(interval.find_last_not_of(" \t") + 1);
        if (interval.empty()) continue;

        size_t dashPos = interval.find('-');
        if (dashPos == std::string::npos) {
            std::cout << "   Неверный формат интервала: " << interval << ". Пропускаем.\n";
            continue;
        }
        std::string startStr = interval.substr(0, dashPos);
        std::string endStr = interval.substr(dashPos + 1);

        auto parseTime = [](const std::string& s) -> double {
            size_t colon = s.find(':');
            if (colon == std::string::npos) return -1.0;
            try {
                int h = std::stoi(s.substr(0, colon));
                int m = std::stoi(s.substr(colon + 1));
                if (h < 0 || h > 23 || m < 0 || m > 59) return -1.0;
                return h + m / 60.0;
            }
            catch (...) { return -1.0; }
            };

        double t1 = parseTime(startStr);
        double t2 = parseTime(endStr);
        if (t1 < 0 || t2 < 0 || t2 <= t1) {
            std::cout << "   Ошибка в интервале " << interval << ". Пропускаем.\n";
            continue;
        }
        totalHours += (t2 - t1);
    }
    return totalHours;
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

    std::vector<double> predictFreeEnergy(const std::vector<double>& plannedLoadHours) {
        double E = E_current_;
        std::vector<double> predicted;
        predicted.reserve(plannedLoadHours.size());

        for (double loadHours : plannedLoadHours) {
            double u = clamp(loadHours / 24.0, 0.0, 1.0);
            const double dt = 1.0;
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

    int suggestOptimalDay(double taskLoadHours,
        int deadlineDay,
        const std::vector<double>& plannedLoad)
    {
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
    return buf;
}

const char* dayOfWeekShortName(int wday) {
    static const char* names[] = { "Вс","Пн","Вт","Ср","Чт","Пт","Сб" };
    return names[wday % 7];
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
    };

    SmartDiary(const Date& start, const Date& end,
        double alpha, double beta, double maxE, double initialE,
        const std::vector<double>& weeklyLoadTemplate)
        : model_(alpha, beta, maxE),
        startDate_(start),
        endDate_(end)
    {
        model_.setInitialEnergy(initialE);
        totalDays_ = daysBetween(start, end) + 1;
        dates_.resize(totalDays_);
        weekDays_.resize(totalDays_);
        baseLoad_.resize(totalDays_, 0.0);
        addedLoad_.resize(totalDays_, 0.0);

        for (int i = 0; i < totalDays_; ++i) {
            Date d = addDays(startDate_, i);
            dates_[i] = d;
            std::tm t = makeDate(d.year, d.month, d.day);
            weekDays_[i] = t.tm_wday;
            if (!weeklyLoadTemplate.empty()) {
                int wd = weekDays_[i];
                if (wd >= 0 && wd < 7)
                    baseLoad_[i] = weeklyLoadTemplate[wd];
            }
        }
    }

    std::vector<double> getTotalLoad() const {
        std::vector<double> total(totalDays_);
        for (int i = 0; i < totalDays_; ++i)
            total[i] = baseLoad_[i] + addedLoad_[i];
        return total;
    }

    std::vector<double> getEnergyForecast() {
        return model_.predictFreeEnergy(getTotalLoad());
    }

    bool addTask(const std::string& name, double hours, const Date& deadlineDate) {
        int deadlineIdx = dayIndex(deadlineDate);
        if (deadlineIdx < 0) {
            std::cout << "Дедлайн вне календарного периода.\n";
            return false;
        }
        int bestDay = model_.suggestOptimalDay(hours, deadlineIdx, getTotalLoad());
        if (bestDay < 0) {
            std::cout << "Невозможно вписать задачу \"" << name << "\" без критического истощения.\n";
            return false;
        }
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

    void printCalendar() {
        auto energy = getEnergyForecast();
        auto totalLoad = getTotalLoad();
        std::cout << "\nКалендарь занятости и прогноз энергии:\n"
            << "Дата       | День | Нагрузка(ч) | Своб. ёмкость\n"
            << "------------------------------------------------------\n";
        for (int i = 0; i < totalDays_; ++i) {
            std::cout << std::left << std::setw(10) << dateToString(dates_[i]) << " | "
                << std::setw(3) << dayOfWeekShortName(weekDays_[i]) << " | "
                << std::setw(11) << totalLoad[i] << " | "
                << std::fixed << std::setprecision(3) << energy[i] << "\n";
        }
        if (!tasks_.empty()) {
            std::cout << "\nСписок задач:\n";
            for (const auto& t : tasks_) {
                std::cout << " - " << t.name << " (" << t.hours << " ч), дедлайн: "
                    << dateToString(dates_[t.deadlineDay])
                    << ", выполнить: " << dateToString(dates_[t.assignedDay]) << "\n";
            }
        }
    }

    void saveToFile(const std::string& filename,
        double alpha, double beta, double maxE, double initialE,
        const std::vector<double>& weekTemplate) const {
        std::ofstream f(filename);
        if (!f) {
            std::cerr << "Ошибка сохранения файла!\n";
            return;
        }
        f.imbue(std::locale("C")); // фиксированный формат
        f << alpha << "\n" << beta << "\n" << maxE << "\n" << initialE << "\n";
        for (double v : weekTemplate) f << v << " ";
        f << "\n";
        f << tasks_.size() << "\n";
        for (const auto& t : tasks_) {
            f << t.name << "\n"
                << t.hours << "\n"
                << t.deadlineDay << "\n"
                << t.assignedDay << "\n"
                << t.completed << "\n";
        }
        std::cout << "Данные сохранены в " << filename << "\n";
    }

    static SmartDiary loadFromFile(const std::string& filename,
        const Date& start, const Date& end) {
        std::ifstream f(filename);
        if (!f) throw std::runtime_error("Не удалось открыть файл для загрузки.");
        f.imbue(std::locale("C"));

        double alpha, beta, maxE, initialE;
        f >> alpha >> beta >> maxE >> initialE;
        std::vector<double> weekTemplate(7);
        for (int i = 0; i < 7; ++i) f >> weekTemplate[i];

        SmartDiary diary(start, end, alpha, beta, maxE, initialE, weekTemplate);

        int numTasks;
        f >> numTasks;
        f.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        for (int i = 0; i < numTasks; ++i) {
            Task t;
            std::getline(f, t.name);
            f >> t.hours >> t.deadlineDay >> t.assignedDay >> t.completed;
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
    std::vector<int>  weekDays_;
    std::vector<double> baseLoad_;
    std::vector<double> addedLoad_;
    std::vector<Task> tasks_;

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
// 4. Главное меню (русифицированное)
// ----------------------------------------------------------------------
int main() {
    setupRussianLocale(); // обязательно в начале

    Date startDate = { 2026, 5, 1 };
    Date endDate = { 2026, 7, 1 };
    const std::string saveFileName = "diary_data.txt";

    std::cout << "=== УМНЫЙ ЕЖЕДНЕВНИК (1 мая – 1 июля 2026) ===\n";

    SmartDiary* diaryPtr = nullptr;
    double alpha = 2.5, beta = 0.8, maxE = 1.0, initialE = 0.9;
    std::vector<double> weekTemplate(7, 0.0);

    // Попытка загрузить сохранённые данные
    std::ifstream testFile(saveFileName);
    if (testFile.good()) {
        testFile.close();
        std::cout << "Найдены сохранённые данные. Загрузить их? (y/n): ";
        std::string ans;
        std::getline(std::cin, ans);
        if (!ans.empty() && (ans[0] == 'y' || ans[0] == 'Y')) {
            try {
                diaryPtr = new SmartDiary(SmartDiary::loadFromFile(saveFileName, startDate, endDate));
                std::cout << "Данные загружены.\n";
            }
            catch (const std::exception& e) {
                std::cout << "Ошибка загрузки: " << e.what() << "\nНачинаем заново.\n";
            }
        }
    }

    if (!diaryPtr) {
        // Первичный ввод параметров
        std::cout << "\nВведите параметры модели восстановления энергии:\n";
        alpha = inputDouble("  Коэффициент утомления alpha: ");
        beta = inputDouble("  Коэффициент восстановления beta: ");
        maxE = inputDouble("  Максимальная ёмкость (E_max): ");
        initialE = inputDouble("  Начальная энергия (0 – " + std::to_string(maxE) + "): ");

        // Ввод полного расписания по дням недели
        std::cout << "\nТеперь опишите свою обычную занятость по дням недели.\n";
        std::cout << "Вводите занятые интервалы в формате ЧЧ:ММ-ЧЧ:ММ (несколько через запятую),\n";
        std::cout << "или просто нажмите Enter, если день свободен.\n\n";
        const char* dayNames[] = { "Вс", "Пн", "Вт", "Ср", "Чт", "Пт", "Сб" };
        for (int i = 0; i < 7; ++i) {
            weekTemplate[i] = inputDayIntervals(dayNames[i]);
        }

        diaryPtr = new SmartDiary(startDate, endDate, alpha, beta, maxE, initialE, weekTemplate);
    }

    SmartDiary& diary = *diaryPtr;

    int choice = 0;
    do {
        std::cout << "\nМЕНЮ:\n"
            << "1. Показать календарь и прогноз энергии\n"
            << "2. Добавить задачу\n"
            << "3. Сохранить данные\n"
            << "0. Выход\n"
            << "Ваш выбор: ";
        std::cin >> choice;
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');

        switch (choice) {
        case 1:
            diary.printCalendar();
            break;
        case 2: {
            std::string name;
            double hours;
            int d, m;
            std::cout << "Название задачи: ";
            std::getline(std::cin, name);
            hours = inputDouble("Трудоёмкость (часы): ");
            std::cout << "Дедлайн (день месяц, например 15 6): ";
            std::cin >> d >> m;
            std::cin.ignore();
            Date deadline = { 2026, m, d };
            diary.addTask(name, hours, deadline);
            break;
        }
        case 3:
            diary.saveToFile(saveFileName, alpha, beta, maxE, initialE, weekTemplate);
            break;
        case 0:
            diary.saveToFile(saveFileName, alpha, beta, maxE, initialE, weekTemplate);
            std::cout << "До свидания!\n";
            break;
        default:
            std::cout << "Неверный пункт.\n";
        }
    } while (choice != 0);

    delete diaryPtr;
    return 0;
}