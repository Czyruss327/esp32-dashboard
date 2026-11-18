<?php

use Illuminate\Database\Migrations\Migration;
use Illuminate\Database\Schema\Blueprint;
use Illuminate\Support\Facades\Schema;

return new class extends Migration {
    public function up(): void
    {
        Schema::table('device_commands', function (Blueprint $table) {
            $table->boolean('outputs_on')->default(false)->after('mode');
        });

        Schema::table('readings', function (Blueprint $table) {
            $table->boolean('outputs_on')->nullable()->after('alert');
        });
    }

    public function down(): void
    {
        Schema::table('device_commands', function (Blueprint $table) {
            $table->dropColumn('outputs_on');
        });

        Schema::table('readings', function (Blueprint $table) {
            $table->dropColumn('outputs_on');
        });
    }
};
