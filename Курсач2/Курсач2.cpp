#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <stdexcept>

// ----------------------------------------------------------------------
// 1. Модель на основе дифференциального уравнения восстановления энергии
// ----------------------------------------------------------------------
class EnergyModel {
public:
    EnergyModel(double alpha, double beta, double maxEnergy = 1.0)
        : alpha_(alpha), beta_(beta), E_max_(maxEnergy)
    {
        if (alpha <= 0 || beta <= 0 || E_max_ <= 0)
            throw std::invalid_argument("Parameters must be positive");
    }

    // Установка начального уровня свободной энергии
    void setInitialEnergy(double E0) {
        if (E0 < 0 || E0 > E_max_)
            throw std::domain_error("Initial energy out of range [0, E_max]");
        E_current_ = E0;
    }

    double getCurrentEnergy() const { return E_current_; }

    // Прогноз свободной энергии на numDays вперёд при заданной загрузке
    // plannedLoadHours[i] – количество учебных часов в день i (0..24)
    // Возвращает вектор энергии на КОНЕЦ каждого дня (после учёта нагрузки и восстановления)
    std::vector<double> predictFreeEnergy(const std::vector<double>& plannedLoadHours) {
        double E = E_current_;
        std::vector<double> predicted;
        predicted.reserve(plannedLoadHours.size());

        for (double loadHours : plannedLoadHours) {
            // нормируем нагрузку: доля дня (24 часа)
            double u = loadHours / 24.0;
            if (u < 0.0) u = 0.0;
            if (u > 1.0) u = 1.0;
            // Длительность одного дня в условных единицах времени: t_day = 1.0
            const double dt = 1.0;
            // Аналитическое решение для постоянной u в течение дня
            if (u == 0.0) {
                // только восстановление
                E = E_max_ - (E_max_ - E) * std::exp(-beta_ * dt);
            }
            else {
                // E' = -alpha*u*E + beta*(E_max - E) = -(alpha*u + beta)*E + beta*E_max
                double a = alpha_ * u + beta_;
                double b_over_a = (beta_ * E_max_) / a; // стационарное решение E_inf
                E = b_over_a - (b_over_a - E) * std::exp(-a * dt);
            }
            predicted.push_back(E);
        }

        return predicted;
    }

    // Поиск оптимального дня для новой задачи
    // taskLoadHours – трудоёмкость задачи в часах,
    // deadlineDay – крайний срок (индекс дня, начиная с 0 = сегодня)
    // currentDayIndex – сегодняшний индекс дня (обычно 0)
    // plannedLoad – будущая запланированная нагрузка (без новой задачи) на дни [currentDayIndex, deadlineDay]
    // Возвращает индекс лучшего дня для постановки задачи, или -1 при невозможности
    int suggestOptimalDay(double taskLoadHours,
        int deadlineDay,
        const std::vector<double>& plannedLoad)
    {
        if (deadlineDay < 0 || deadlineDay >= static_cast<int>(plannedLoad.size()))
            throw std::out_of_range("deadlineDay out of plannedLoad range");

        double bestMetric = -1e9;
        int bestDay = -1;
        const double minEnergyThreshold = 0.1 * E_max_; // минимально допустимая энергия

        for (int day = 0; day <= deadlineDay; ++day) {
            // копируем плановую нагрузку и добавляем задачу в рассматриваемый день
            std::vector<double> modifiedLoad = plannedLoad;
            modifiedLoad[day] += taskLoadHours;

            // рассчитываем прогноз энергии с добавленной задачей
            double E0_backup = E_current_;
            std::vector<double> energyAfter = predictFreeEnergy(modifiedLoad);
            E_current_ = E0_backup; // восстанавливаем состояние

            // проверяем, что энергия нигде не падает ниже порога
            bool feasible = true;
            double minEnergy = E_max_;
            for (double e : energyAfter) {
                if (e < minEnergyThreshold) {
                    feasible = false;
                    break;
                }
                minEnergy = std::min(minEnergy, e);
            }

            if (!feasible) continue;   // недопустимо

            // метрика: максимизируем минимальную энергию за период, а при равенстве – конец периода
            double metric = minEnergy;
            // можно добавить небольшой вес итоговой энергии:
            metric += 0.001 * energyAfter.back();

            if (metric > bestMetric) {
                bestMetric = metric;
                bestDay = day;
            }
        }

        return bestDay;
    }

private:
    double alpha_, beta_, E_max_;
    double E_current_ = E_max_; // по умолчанию полностью свободен
};

// ----------------------------------------------------------------------
// 2. Заглушка для модели скользящего среднего (для будущего сравнения)
// ----------------------------------------------------------------------
class MovingAverageModel {
public:
    // Прогноз на основе усечённого среднего по дням недели
    // history – матрица: [неделя][день] (0..6) с фактическими часами
    // weekDay – день недели (0..6) для прогноза
    double predict(int weekDay, const std::vector<std::vector<double>>& history) const {
        if (history.empty()) return 0.0;
        double sum = 0.0;
        int count = 0;
        for (const auto& week : history) {
            if (weekDay < static_cast<int>(week.size())) {
                sum += week[weekDay];
                ++count;
            }
        }
        return count > 0 ? sum / count : 0.0;
    }
};

// ----------------------------------------------------------------------
// Пример использования (можно разместить в main)
// ----------------------------------------------------------------------
int main() {
    setlocale(LC_ALL, "russian");
    // Параметры модели (подбираются по историческим данным)
    double alpha = 2.5;   // коэффициент утомления
    double beta = 0.8;   // коэффициент восстановления
    double maxE = 1.0;   // максимальная свободная энергия

    EnergyModel model(alpha, beta, maxE);
    model.setInitialEnergy(0.9); // на утро понедельника – 90% энергии

    // План нагрузки на ближайшие 7 дней (часы)
    std::vector<double> plannedHours = { 6, 8, 5, 7, 0, 0, 4 }; // будни + выходные
    auto predictedEnergy = model.predictFreeEnergy(plannedHours);

    std::cout << "Прогноз свободной энергии по дням недели (конец дня):\n";
    for (size_t i = 0; i < predictedEnergy.size(); ++i) {
        std::cout << "День " << i << ": " << predictedEnergy[i] << std::endl;
    }

    // Поиск оптимального дня для задачи (трудоёмкость 3 часа, дедлайн – 5-й день)
    double taskHours = 3.0;
    int deadline = 5;
    int bestDay = model.suggestOptimalDay(taskHours, deadline, plannedHours);
    if (bestDay >= 0) {
        std::cout << "\nЛучший день для выполнения задачи: " << bestDay
            << " (прогнозная энергия в конце дня "
            << model.predictFreeEnergy(plannedHours)[bestDay] << ")\n";
    }
    else {
        std::cout << "\nНевозможно вписать задачу в указанный период без критического истощения.\n";
    }

    return 0;
}