<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

return new class extends Migration {
    public function up(): void
    {
        Schema::create('alert_logs', function (Blueprint $t) {
            $t->id();
            $t->string('event', 10);             // start | clear
            $t->float('temperature')->nullable();
            $t->float('humidity')->nullable();
            $t->timestamps();
        });
    }

    public function down(): void
    {
        Schema::dropIfExists('alert_logs');
    }
};
