#pragma once

#include <iostream>

// #include <print>
#include <flecs.h>
#include <vector>

#include <variant>

template<class... Ts>
struct overloads : Ts... { using Ts::operator()...; };
 
// the variant to visit
using var_t = std::variant<int, double, float>;
void TestVariant() {
    const auto visitor = overloads
    {
        [](int i) { std::cout << "int = " << i << "\n"; },
        [](double d) { std::cout << "double = " << d << "\n"; },
        [](float f) { std::cout << "float = " << f << "\n"; },
    };

    var_t v1 = 42, v2 = 1.0, v3 = 3.f;
    std::visit(visitor, v1);
    std::visit(visitor, v2);
    std::visit(visitor, v3);
}

struct Position {
    double x;
    double y;
};

struct Velocity {
    double x;
    double y;
};

struct Serializable {};


// Tag types
struct Eats { };
struct Apples { };

void test_flex(int argc, char *argv[]) {
    // Create the world
    flecs::world ecs;

    ecs.component<Position>()
        .member<double>("x")
        .member<double>("y");

    ecs.component<Velocity>()
        .member<double>("x")
        .member<double>("y");

    auto settings_json = ecs.to_json();

    // Register system
    ecs.system<Position, Velocity>()
        .each([](Position& p, Velocity& v) {
            p.x += v.x;
            p.y += v.y;
        });

    // Create an entity with name Bob, add Position and food preference
    flecs::entity Bob = ecs.entity("Bob")
        .set(Position{0, 0})
        .set(Velocity{1, 2})
        .add<Eats, Apples>();

    // Show us what you got
    std::cout << Bob.name() << "'s got [" << Bob.type().str() << "]\n";
    const Position& old_p = Bob.get<Position>();
    std::cout << "~~~~ Old: " << Bob.name() << "'s position is {" << old_p.x << ", " << old_p.y << "}\n";

    ecs_world_to_json_desc_t desc = {
        .serialize_builtin = false,
        .serialize_modules = false,
    };

    auto json = ecs.to_json();

    std::cout << json << std::endl;

    // Run systems twice. Usually this function is called once per frame
    ecs.progress();
    ecs.progress();

    flecs::entity bob2 = ecs.entity("Bob2")
        .set(Position{0, 0})
        .set(Velocity{1, 2})
        .add<Eats, Apples>();


    // See if Bob has moved (he has)
    const Position& p = Bob.get<Position>();
    std::cout << "~~~~ New: " << Bob.name() << "'s position is {" << p.x << ", " << p.y << "}\n";

    bob2.set(Position{100, 100});

    // Output
    //  Bob's got [Position, Velocity, (Identifier,Name), (Eats,Apples)]
    //  Bob's position is {2, 4}

    ecs.reset();


    
    flecs::from_json_desc_t decs {
        .strict = false,
    };
    ecs.from_json(json, &decs);
    auto restored_bob = ecs.lookup("Bob");
    const Position& restored_p = restored_bob.get<Position>();
    std::cout << "~~~~ Restored: " << restored_bob.name() << "'s position is {" << restored_p.x << ", " << restored_p.y << "}\n";

    auto bob_new = ecs.lookup("Bob2");
    std::cout << "~~~~ [ECS] New Bob 2: " << bob_new.is_valid() << "\n";

    // auto bob2_new = ecs3.lookup("Bob2");
    // std::cout << "~~~~ [ECS3] New Bob 2: " << bob2_new.is_valid() << "\n";

    std::cout << ecs.to_json() << std::endl;
}

void test_flex2(int argc, char *argv[]) {
    flecs::world world;

    world.component<Position>()
        .member<double>("x")
        .member<double>("y");

    world.component<Velocity>()
        .member<double>("x")
        .member<double>("y");

    world.component<Serializable>();

    flecs::entity serializable_parent = world.entity("Serializable_parent").add<Serializable>();
    flecs::entity bob = world.entity("Bob").set(Position{10, 10}).set(Velocity{1, 0});
    flecs::entity alice = world.entity("Alice").set(Position{11, 10}).set(Velocity{1, 0}).child_of(serializable_parent);
    flecs::entity clara = world.entity("Clara").set(Position{100, 0}).set(Velocity{2, 2}).child_of(serializable_parent);

    // world.snapshot()

    // std::cout << "1 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << std::endl;
    // std::cout << world.to_json() << std::endl;
    // std::cout << "2 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << std::endl;
    // ecs_entity_to_json_desc_t e_to_j = ECS_ENTITY_TO_JSON_INIT;
    // // e_to_j.serialize_builtin = true;
    // e_to_j.serialize_alerts = true;
    // auto serializable_parent_json_2 = serializable_parent.to_json(&e_to_j);
    // std::cout << serializable_parent_json_2 << std::endl;
    // std::cout << "2.1 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << std::endl;
    // auto serializable_parent_json = world.to_json(&serializable_parent);
    // std::cout << serializable_parent_json << std::endl;
    // std::cout << "2.2 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << std::endl;
    // auto bob_json = world.to_json(&bob);
    // std::cout << bob_json << std::endl;
    // std::cout << "3 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << std::endl;
    // serializable_parent.destruct();
    // world.progress();
    // std::cout << world.to_json() << std::endl;
    // std::cout << "4 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << std::endl;
    // flecs::entity deserialized_parent;
    // world.from_json(&deserialized_parent, serializable_parent_json_2);
    // std::cout << world.to_json() << std::endl;
    // std::cout << "5 ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~" << std::endl;
}