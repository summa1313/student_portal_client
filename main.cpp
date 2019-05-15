#include <iostream>
#include <iomanip>
#include <vector>
#include <mysql.h>
using namespace std;

MYSQL *connection, mysql;

/**
 * Structure for Student details
 */
struct Student {
    Student() {
        id = 0;
    }
    int id;
    string name;
    string address;
};

/**
 * Structure for Course information
 */
struct Course {
    Course() {
        credits = 0;
        year = 0;
        enrollment = 0;
        maxenrollment = 0;
    }
    string id;
    string name;
    int credits;
    string semester;
    int year;
    string grade;
    string deptid;
    
    int enrollment;
    int maxenrollment;
    string textbook;
    string lecturer;
    string classroom;
    string classtime;
};

/**
 * Execute MySQL query and return result
 */
MYSQL_RES* execSqlQuery(const string& sql)
{
    mysql_query(&mysql, sql.c_str());
    
    MYSQL_RES* result = mysql_store_result(connection);
    
    if(mysql_errno( connection ) || mysql_warning_count(connection))
        cout << mysql_error(&mysql) << endl;

    // return result if there are rows
    if(result && mysql_num_rows(result) > 0)
        return result;
    
    // otherwise cleanup automatically
    if(result)
        mysql_free_result(result);
    return nullptr;
}

/**
 * Returns current quarter: Q1, Q2, Q3
 */
string getCurrentSemester()
{
    time_t t = time(NULL);
    tm* timePtr = localtime(&t);
    
    // Assuming:
    // Sep-Nov: Q1
    // Dec-Feb: Q2
    // Mar-May: Q3
    // Jun-Aug: 04
    if((timePtr->tm_mon == 12) || (timePtr->tm_mon <= 2))
        return "Q2";
    else if(timePtr->tm_mon <= 5)
        return "Q3";
    else if(timePtr->tm_mon <= 8)
        return "Q4";
    else
        return "Q1";
}

/**
 * Returns current year
 */
int getCurrentYear()
{
    time_t t = time(NULL);
    tm* timePtr = localtime(&t);
    
    return 1900+timePtr->tm_year;
}

/**
 * Create storage procedures
 */
void db_createProcedures()
{
    // TRIGGER
    // If the Enrollment number goes below 50% of the MaxEnrollment, then a warning message should be shown on the screen. Implement this using Triggers. [10]
    mysql_query(&mysql, "DROP TRIGGER IF EXISTS below_limit;");
    
    string trigger_sql = "CREATE TRIGGER below_limit BEFORE UPDATE ON uosoffering FOR EACH ROW BEGIN \
                            IF (new.Enrollment < new.MaxEnrollment/2) THEN \
                                set @message_text = CONCAT('Warning: ', new.UoSCode, ' - enrollment is below 50%'); \
                                SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT = @message_text; \
                            END IF; \
                          END";
    mysql_query(&mysql, trigger_sql.c_str());
    if(mysql_errno( connection ) || mysql_warning_count(connection))
        cout << mysql_error(&mysql) << endl;
    
    // STORED PROCEDURE : enroll
    mysql_query(&mysql, "DROP procedure IF EXISTS `enroll_student`;");
    
    string enroll_sql = "CREATE DEFINER=`root`@`localhost` PROCEDURE `enroll_student`(IN in_course_id char(8), IN in_semester char(2), IN in_year int, IN in_student_id int) \n\
        BEGIN \n\
        DECLARE prerequisites varchar(256); \n\
        START TRANSACTION; \n\
        # check course exists \n\
        IF (SELECT EXISTS( select UoSCode from uosoffering where UoSCode=in_course_id and Semester=in_semester and Year=in_year )) THEN \n\
            # check enrollment places \n\
            IF ( (select Enrollment<MaxEnrollment from uosoffering where UoSCode=in_course_id and Semester=in_semester and Year=in_year) ) THEN \n\
                # if already taken \n\
                IF(SELECT EXISTS( select UoSCode from transcript where UoSCode=in_course_id and Semester=in_semester and Year=in_year and StudId=in_student_id and Grade is not null and Grade != 'F')) THEN \n\
                    SELECT 'Already taken'; \n\
                    ROLLBACK; \n\
                ELSE \n\
                    # if not enrolled yet \n\
                    IF(SELECT EXISTS(select UoSCode from transcript where UoSCode=in_course_id and Semester=in_semester and Year=in_year and StudId=in_student_id and (Grade is null or Grade = 'F'))) THEN \n\
                        SELECT 'Already enrolled'; \n\
                        ROLLBACK; \n\
                    ELSE \n\
                        # check prerequisites \n\
                        SELECT GROUP_CONCAT(r.PrereqUoSCode SEPARATOR ' ') into prerequisites FROM requires r \n\
                        LEFT JOIN transcript t on (t.UoSCode=r.PrereqUoSCode and t.StudId=in_student_id) \n\
                        WHERE r.uoscode=in_course_id and (t.grade is null or t.grade='F' or t.grade='I'); \n\
                        IF prerequisites IS NOT NULL THEN \n\
                            SELECT CONCAT('Prerequisites not met: ', prerequisites); \n\
                            ROLLBACK; \n\
                        ELSE \n\
                            # a new entry in the Transcript table shall be created with a NULL grade, \n\
                            # Enrollment attribute of the corresponding course shall be increased by one. \n\
                            insert into transcript(StudId, UoSCode, Semester, Year, Grade) VALUES(in_student_id, in_course_id, in_semester, in_year, null); \n\
                            update uosoffering set Enrollment=Enrollment+1 where UoSCode=in_course_id and Semester=in_semester and Year=in_year; \n\
                            SELECT 'OK'; \n\
                            COMMIT; \n\
                        END IF; \n\
                    END IF; \n\
                END IF; \n\
            ELSE \n\
                SELECT 'Not seats available'; \n\
                ROLLBACK; \n\
            END IF; \n\
        ELSE \n\
            SELECT 'Course not offered'; \n\
            ROLLBACK; \n\
        END IF; \n\
        END";
    mysql_query(&mysql, enroll_sql.c_str());
    if(mysql_errno( connection ) || mysql_warning_count(connection))
        cout << mysql_error(&mysql) << endl;
    
    // STORED PROCEDURE : withdraw
    mysql_query(&mysql, "DROP procedure IF EXISTS `withdraw_student`;");
    
    string withdraw_sql = "CREATE DEFINER=`root`@`localhost` PROCEDURE `withdraw_student`(IN in_course_id char(8), IN in_semester char(2), IN in_year int, IN in_student_id int) \
    BEGIN \n\
        # start transaction \n\
        START TRANSACTION; \n\
        # check if student is enrolled \n\
        IF( SELECT EXISTS( select UoSCode from transcript where UoSCode=in_course_id and Semester=in_semester and Year=in_year and StudId=in_student_id)) THEN \n\
            # check if withdraw possible \n\
            IF( SELECT EXISTS( select UoSCode from transcript where UoSCode=in_course_id and Semester=in_semester and Year=in_year and StudId=in_student_id and Grade is null)) THEN \n\
                # Transcript entry shall be removed and the current Enrollment number of the corresponding course shall be decreased by one. \n\
                DELETE FROM transcript WHERE UoSCode=in_course_id and Semester=in_semester and Year=in_year and StudId=in_student_id; \n\
                UPDATE uosoffering SET Enrollment=Enrollment-1 where UoSCode=in_course_id and Semester=in_semester and Year=in_year; \n\
                SELECT 'OK'; \n\
                COMMIT; \n\
            ELSE \n\
                SELECT 'Cant withdraw from a course with a grade'; \n\
                ROLLBACK; \n\
            END IF; \n\
        ELSE \n\
            SELECT 'Not enrolled'; \n\
            ROLLBACK; \n\
        END IF; \n\
    END";
    mysql_query(&mysql, withdraw_sql.c_str());
    if(mysql_errno( connection ) || mysql_warning_count(connection))
        cout << mysql_error(&mysql) << endl;
}

/**
 * Query courses available for enrollment in current quarter
 */
vector<Course> db_queryEnrollmentCourses()
{
    vector<Course> courses;
    
    string semester = getCurrentSemester();
    int year = getCurrentYear();
    
    // Query courses available for enrollment in current quarter
    MYSQL_RES* result = execSqlQuery("SELECT U.UoSCode, U.DeptId, U.UoSName, U.Credits, V.Enrollment, V.Maxenrollment, f.Name, L.ClassTime, L.ClassroomId \
                                     FROM unitofstudy U, uosoffering V \
                                     LEFT JOIN faculty f on (f.Id=V.InstructorId) \
                                     LEFT JOIN lecture L on (L.UoSCode=V.UoSCode and L.Semester=V.Semester and L.Year=V.Year) \
                                     WHERE U.UoSCode=V.UoSCode AND V.Semester='"+semester+"' AND V.Year='"+to_string(year)+"'");
    if(result)
    {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(result)))
        {
            Course c1;
            c1.id = row[0];
            c1.deptid = row[1];
            c1.name = row[2];
            c1.credits = atoi(row[3]);
            c1.enrollment = atoi(row[4]);
            c1.maxenrollment = atoi(row[5]);
            c1.lecturer = row[6] ? row[6] : "";
            c1.classtime = row[7] ? row[7] : "";
            c1.classroom = row[8] ? row[8] : "";
            courses.push_back(c1);
        }
        mysql_free_result(result);
    }
    return courses;
}

/**
 * Query courses from student transcript
 */
vector<Course> db_queryStudentTranscript(int user_id)
{
    vector<Course> courses;
    
    // The course details should include:
    //   the course number and title,
    //   the year and quarter when the student took the course,
    //   the number of enrolled students,
    //   the maximum enrollment and
    //   the lecturer (name),
    //   the grade scored by the student.
    
    MYSQL_RES* result = execSqlQuery("SELECT u.UoSCode, u.UoSName, u.Credits, t.Semester, t.Year, t.Grade, o.Enrollment, o.MaxEnrollment, f.Name \
                                     FROM unitofstudy u \
                                     INNER JOIN transcript t on (t.UoSCode=u.UoSCode and t.StudId="+to_string(user_id)+") \
                                     INNER JOIN uosoffering o on (o.UoSCode=u.UoSCode and o.Semester=t.Semester and o.Year=t.Year) \
                                     LEFT JOIN faculty f on (f.Id=o.InstructorId) \
                                     ORDER BY t.Semester, t.Year");
    if(result)
    {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(result)))
        {
            Course c1;
            c1.id = row[0];
            c1.name = row[1];
            c1.credits = atoi(row[2]);
            c1.semester = row[3];
            c1.year = atoi(row[4]);
            c1.grade = row[5] ? row[5] : "";
            c1.enrollment = atoi(row[6]);
            c1.maxenrollment = atoi(row[7]);
            c1.lecturer = row[8] ? row[8] : "";
            courses.push_back(c1);
        }
        mysql_free_result(result);
    }
    
    return courses;
}

/**
 * Query currently enrolled courses list for student
 */
vector<Course> db_queryCurrentCourses(int user_id)
{
    vector<Course> courses;
    
    string semester = getCurrentSemester();
    int year = getCurrentYear();
    
    // Query list of current courses. Course Id and Name3213
    MYSQL_RES* result = execSqlQuery("SELECT T.UoSCode, U.UoSName \
                                     FROM transcript T, unitofstudy U \
                                     WHERE  T.UoSCode=U.UoSCode AND StudId = '"+to_string(user_id)+"' AND Semester = '"+semester+"' \
                                     AND Year = '"+to_string(year)+"' AND T.Grade is NULL");
    if(result)
    {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(result)))
        {
            Course c1;
            c1.id = row[0];
            c1.name = row[1];
            courses.push_back(c1);
        }
        mysql_free_result(result);
    }
    return courses;
}

/**
 * Query student details
 */
Student db_queryStudent(int user_id)
{
    Student student;
    string sql = "SELECT Name, Address FROM student where Id=" + to_string(user_id);
    MYSQL_RES *result = execSqlQuery(sql);
    if(result)
    {
        MYSQL_ROW row = mysql_fetch_row(result);
        student.id = user_id;
        student.name = row[0];
        student.address = row[1];
        mysql_free_result(result);
    }
    return student;
}

/**
 * Change password
 */
void db_changePassword(int user_id, const string& password)
{
    // escape string
    char value[100];
    mysql_real_escape_string(&mysql, value, password.c_str(), password.size());
    
    // exec update query: UPDATE table SET field=value
    execSqlQuery("START TRANSACTION;");
    execSqlQuery("UPDATE Student S SET S.Password='"+string(value)+"' WHERE S.Id='"+to_string(user_id)+"';");
    execSqlQuery("COMMIT;");
}

/**
 * Change address
 */
void db_changeAddress(int user_id, const string& address)
{
    // escape string
    char value[100];
    mysql_real_escape_string(&mysql, value, address.c_str(), address.size());
    
    // exec update query: UPDATE table SET field=value
    execSqlQuery("START TRANSACTION;");
    execSqlQuery("UPDATE Student S SET S.Address='"+string(value)+"' WHERE S.Id='"+to_string(user_id)+"';");
    execSqlQuery("COMMIT;");
}

/**
 * Find user with username/password
 */
int db_login(const string& username, const string& password)
{
    // select student id with username and password provided
    // return student id
    int ID = 0;
    string sql = "SELECT Id FROM student WHERE Id='"+username+"' AND Password = '"+password+"'";
    MYSQL_RES *result = execSqlQuery(sql);
    if(result)
    {
        MYSQL_ROW row = mysql_fetch_row(result);
        ID = atoi(row[0]);
        mysql_free_result(result);
    }
    return ID;
}

/**
 * Enroll into selected course
 */
void db_enroll_into(const string& course_id, const string& semester, int year, int user_id)
{
    MYSQL_RES* result = execSqlQuery("CALL enroll_student('"+course_id+"', '"+semester+"', "+to_string(year)+", "+to_string(user_id)+")");
    if(result)
    {
        MYSQL_ROW row = mysql_fetch_row(result);
        
        // print message from stored procedure
        string response = row[0];
        cout << response;
        
        mysql_next_result(&mysql);
        mysql_free_result(result);
    }
}

/**
 * Withdraw from course
 */
void db_withdraw(const string& course_id, const string& semester, int year, int user_id)
{
    MYSQL_RES* result = execSqlQuery("CALL withdraw_student('"+course_id+"', '"+semester+"', "+to_string(year)+", "+to_string(user_id)+")");
    if(result)
    {
        MYSQL_ROW row = mysql_fetch_row(result);
        
        // print message from stored procedure
        string response = row[0];
        cout << response;
        
        mysql_next_result(&mysql);
        mysql_free_result(result);
    }
}

/**
 * Query course details
 */
Course db_queryCourseDetails(const string& course_id, int user_id)
{
    Course c1;
    
    // The course details should include:
    //   the course number and title,
    //   the year and quarter when the student took the course
    //   the number of enrolled students,
    //   the maximum enrollment
    //   and the lecturer (name)
    //   the grade scored by the student.
    MYSQL_RES* result = execSqlQuery("SELECT T.UoSCode, X.UoSName, X.Credits, T.Semester, T.Year, L.Classtime, L.ClassroomId, U.Enrollment, U.MaxEnrollment, F.Name,\
                                     U.Textbook, T.Grade \
                                     FROM transcript T, uosoffering U, unitofstudy X, Faculty F, Lecture L \
                                     WHERE T.StudId='"+to_string(user_id)+"' AND U.UoSCode='"+course_id+"' AND U.UoSCode=T.UoSCode AND X.UoSCode=T.UoSCode \
                                     AND T.Semester=U.Semester AND T.Year=U.Year AND U.InstructorId=F.Id AND L.UoSCode=T.UoSCode");
    if(result)
    {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(result)))
        {
            c1.id = row[0];
            c1.name = row[1];
            c1.credits = atoi(row[2]);
            c1.semester = row[3];
            c1.year = atoi(row[4]);
            c1.classtime = row[5];
            c1.classroom = row[6];
            c1.enrollment = atoi(row[7]);
            c1.maxenrollment = atoi(row[8]);
            c1.lecturer = row[9];
            c1.textbook = row[10];
            c1.grade = row[11] ? row[11] : "";
        }
        mysql_free_result(result);
    }
    return c1;
}

void showCourseScreen(const string& course_id, int user_id)
{
    Course course = db_queryCourseDetails(course_id, user_id);
    
    if(course.id.empty())
    {
        cout << "Unable to find information about " << course_id << endl;
        return;
    }
    
    cout << "\n\n\n";
    cout << "-----------------------------" << endl;
    cout << "* Couse details" << endl;
    cout << "-----------------------------" << endl;
    
    cout << "ID      : " << course.id << endl;
    cout << "Name    : " << course.name << endl;
    cout << "Credits : " << course.credits << endl;
    cout << "Semester: " << course.semester << endl;
    cout << "Year    : " << course.year << endl;
    cout << "Time    : " << course.classtime << endl;
    cout << "Room    : " << course.classroom << endl;
    cout << "Lecturer: " << course.lecturer << endl;
    cout << "Textbook: " << course.textbook << endl;
    cout << "Enrolled: " << course.enrollment << endl;
    cout << "Capacity: " << course.maxenrollment << endl;
    cout << "Grade   : " << course.grade << endl;
    
    cout << endl << endl << "Press any key to continue...";
    system("read");
}

void showTranscriptScreen(int user_id)
{
    while(true)
    {
        Student student = db_queryStudent(user_id);
        cout << "\n\n\n";
        cout << "-----------------------------" << endl;
        cout << "* Transcript for " << student.name << endl;
        cout << "-----------------------------" << endl;
        
        vector<Course> courses = db_queryStudentTranscript(user_id);
        cout << endl << "Your courses:" << endl;
        for(int i=0;i<courses.size();i++)
            cout << "  " << courses[i].semester << " " << courses[i].year << "  " << setw(2) << courses[i].grade << "  " << courses[i].id << " " << courses[i].name << ", " << courses[i].enrollment << "/" << courses[i].maxenrollment << ", " << courses[i].lecturer << endl;
        cout << endl;
        
        cout << "Enter course id for details or '0' to go back: ";
        
        string option;
        cin >> option;
        
        if(option == "0")
            break;
        
        showCourseScreen(option, user_id);
    }
}

void showEnrollScreen(int user_id)
{
    cout << "\n\n\n";
    cout << "-----------------------------" << endl;
    cout << "* Enrollment, available courses: " << endl;
    cout << "-----------------------------" << endl;
    
    vector<Course> courses = db_queryEnrollmentCourses();
    for(int i=0;i<courses.size();i++)
    {
        cout << " " << courses[i].id << " " << courses[i].name << "(" << courses[i].credits << "), " << courses[i].lecturer << ", " << courses[i].classtime << ", " << courses[i].classroom << ",  " << courses[i].enrollment << "/" << courses[i].maxenrollment << endl;
    }
    
    cout << "Enter course id: ";
    
    string courseid;
    cin >> courseid;
    
    string semester = getCurrentSemester();
    int year = getCurrentYear();
    
    db_enroll_into(courseid, semester, year, user_id);
    
    cout << endl << endl << "Press any key to continue...";
    system("read");
}

void showWithdrawScreen(int user_id)
{
    cout << "\n\n\n";
    cout << "-----------------------------" << endl;
    cout << "* Withdraw, current courses: " << endl;
    cout << "-----------------------------" << endl;
    
    vector<Course> courses = db_queryCurrentCourses(user_id);
    cout << endl << "Your current courses:" << endl;
    for(int i=0;i<courses.size();i++)
        cout << "  " << courses[i].id << " " << courses[i].name << endl;
    cout << endl;
    
    cout << "Enter course id: ";
    
    string courseid;
    cin >> courseid;
    
    string semester = getCurrentSemester();
    int year = getCurrentYear();
    
    db_withdraw(courseid, semester, year, user_id);
    
    cout << endl << endl << "Press any key to continue...";
    system("read");
}

void showPersonalDetailsScreen(int user_id)
{
    while(true)
    {
        Student student = db_queryStudent(user_id);
        cout << "\n\n\n";
        cout << "-----------------------------" << endl;
        cout << "* Personal details for " << student.name << endl;
        cout << "-----------------------------" << endl;
        
        cout << " ID: " << student.id << endl;
        cout << " Name: " << student.name << endl;
        cout << " Address: " << student.address << endl;
        
        cout << endl << "Choose menu option:" << endl;
        cout << " 1) Change password" << endl;
        cout << " 2) Change address" << endl;
        cout << " 3) Back to previous menu" << endl;
        cout << ":";
        
        int option = 0;
        cin >> option;
        
        if(option == 1)
        {
            string password;
            cout << "Enter new password: ";
            getline (cin, password);
            getline (cin, password);
            db_changePassword(user_id, password);
            cout << "Password changed." << endl;
        }
        else if(option == 2)
        {
            string address;
            cout << "Enter new address: ";
            getline (cin, address);
            getline (cin, address);
            db_changeAddress(user_id, address);
            cout << "Address changed." << endl;
        }
        else if(option == 3)
            break;
    }
    
}

void showStudentScreen(int user_id)
{
    while(true)
    {
        Student student = db_queryStudent(user_id);
        cout << "\n\n\n";
        cout << "-----------------------------" << endl;
        cout << "* Student information for " << student.name << endl;
        cout << "-----------------------------" << endl;
        
        vector<Course> courses = db_queryCurrentCourses(user_id);
        cout << endl << "Your current courses:" << endl;
        for(int i=0;i<courses.size();i++)
            cout << "  " << courses[i].id << " " << courses[i].name << endl;
        cout << endl;
        
        cout << "Choose menu option:" << endl;
        cout << " 1) Transcript" << endl;
        cout << " 2) Enroll" << endl;
        cout << " 3) Withdraw" << endl;
        cout << " 4) Personal Details" << endl;
        cout << " 5) Logout" << endl;
        cout << ":";
        
        int option = 0;
        cin >> option;
        
        if(option == 1)
            showTranscriptScreen(user_id);
        else if(option == 2)
            showEnrollScreen(user_id);
        else if(option == 3)
            showWithdrawScreen(user_id);
        else if(option == 4)
            showPersonalDetailsScreen(user_id);
        else if(option == 5)
            break;
    }
}

void showLoginScreen()
{
    while(true)
    {
        cout << "\n\n\n";
        cout << "-----------------------------" << endl;
        cout << "* Welcome to student portal" << endl;
        cout << "-----------------------------" << endl;
        
        string username, password;
        cout << "Enter your username: ";
        cin >> username;
        cout << "Enter your password: ";
        cin >> password;
        
        int user_id = db_login(username, password);
        if(user_id != 0)
            showStudentScreen(user_id);
        else
            cout << "Your username or password is incorrect. Try again." << endl;
    }
}

int main()
{
    mysql_init(&mysql);
    connection = mysql_real_connect(&mysql, "localhost", "root", "123",
                                    "project3-nudb", 0, 0, 0);
    
    if (connection != NULL)
    {
        // create storage procedures and triggers
        db_createProcedures();
        
        // go to login screen
        showLoginScreen();
    }
    else
        printf("Unable to connect!\n");
    
    return 0;
}

